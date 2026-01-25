#include "frontend/sdl3_frontend.hpp"
#include "ImGuiFileDialog.h"
#include "SDL3/SDL_audio.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include "cart/cart.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/lr35902.hpp"
#include "debugger/breakpoint.hpp"
#include "debugger/debugger.hpp"
#include "debugger/print.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/palette.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <chrono>
#include <exception>
#include <imgui.h>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>

static const char *rom_filters =
    "ROM files (*.gb *.gbc){.gb,.gbc},All files (*.*){.*}";
static const char *bios_filters =
    "BIOS files (*.bin){.bin},All files (*.*){.*}";

static constexpr unsigned max_catchup_cycles = 70'224 / 4;
static constexpr unsigned target_queue_ms = 20;
static constexpr std::uint32_t black = 0xFF000000;

/*
 * ============================================================
 *  Persistent settings helpers
 * ============================================================
 */

Settings Settings::load(const std::string &filename) {
  Settings s;
  std::ifstream file(filename);
  if (file.is_open()) {
    try {
      nlohmann::json j;
      file >> j;
      s = j.get<Settings>();
    } catch (...) { /* Fallback to defaults on corrupt file */
    }
  }
  return s;
}

void Settings::save(const std::string &filename) const {
  std::ofstream file(filename);
  if (file.is_open()) {
    nlohmann::json j = *this;
    file << j.dump(4); // Indented 4 spaces
  }
}

void Settings::add_recent_rom(const std::string &path) {
  // Remove if already exists (so we can move it to top)
  const auto it = std::ranges::remove(recent_roms, path).begin();
  recent_roms.erase(it, recent_roms.end());
  recent_roms.insert(recent_roms.begin(), path);
  // Keep only the last 10 entries
  if (recent_roms.size() > 10) {
    recent_roms.resize(10);
  }
}

/*
 * ============================================================
 *  Frontend constructors and overrides
 * ============================================================
 */

SDL3Frontend::SDL3Frontend() : Frontend() {
  /* SDL3 initialization */
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    throw std::runtime_error(SDL_GetError());

  window = SDL_CreateWindow("GBC", framebuf_width * scale,
                            framebuf_height * scale, SDL_WINDOW_RESIZABLE);
  if (!window)
    throw std::runtime_error(SDL_GetError());

  renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer)
    throw std::runtime_error(SDL_GetError());
  SDL_SetRenderVSync(renderer, 1);

  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, framebuf_width,
                              framebuf_height);
  if (!texture)
    throw std::runtime_error(SDL_GetError());
  SDL_SetTextureScaleMode(texture, SDL_ScaleMode::SDL_SCALEMODE_PIXELART);

  SDL_zero(audio_spec);
  audio_spec.freq = 48000;
  audio_spec.format = SDL_AUDIO_F32;
  audio_spec.channels = 2;
  audio_device =
      SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec);
  if (!audio_device)
    throw std::runtime_error(SDL_GetError());
  audio_stream = SDL_CreateAudioStream(&audio_spec, &audio_spec);
  if (!audio_stream)
    throw std::runtime_error(SDL_GetError());
  if (!SDL_BindAudioStream(audio_device, audio_stream))
    throw std::runtime_error(SDL_GetError());

  /* Load settings */
  settings = Settings::load();

  /* ImGui initialization */
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer))
    throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
  if (!ImGui_ImplSDLRenderer3_Init(renderer))
    throw std::runtime_error("Failed to initialize ImGui SDL renderer backend");

  rom_sel_conf.path = settings.rom_dir;
  bios_sel_conf.path = ".";
  bios_sel_conf.flags = rom_sel_conf.flags =
      ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ReadOnlyFileNameField;
  framebuffers[0] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  framebuffers[1] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  running = true;
  clear();

  if (settings.keybind_preset_index != kCustomPresetIndex) {
    apply_keybind_preset(settings.keybinds, settings.keybind_preset_index);
  }
}

SDL3Frontend::~SDL3Frontend() {
  // Save current settings to disk
  settings.save();

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  if (audio_stream) {
    SDL_UnbindAudioStream(audio_stream);
    SDL_DestroyAudioStream(audio_stream);
  }
  if (audio_device)
    SDL_CloseAudioDevice(audio_device);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

std::array<std::uint32_t, 160 * 144> SDL3Frontend::get_frame() {
  const int idx = front_index.load(std::memory_order_relaxed);
  std::array<std::uint32_t, 160 * 144> arr{};
  for (auto i{0}; i < framebuf_width * framebuf_height; i++)
    arr[idx] = framebuffers[idx][i];
  return arr;
}

void SDL3Frontend::put_pixel(int x, int y, std::uint32_t c) {
  if (x < 0 || x >= framebuf_width || y < 0 || y >= framebuf_height)
    return;

  /* We perform double buffering to prevent screen tearing */
  const int back_index = 1 - front_index.load(std::memory_order_relaxed);
  framebuffers[back_index][y * framebuf_width + x] = c;

  /* Frame completion can be indicated by the fact that we are placing
   * the last pixel in the frame, so we need to swap buffers here. */
  if (x + 1 == framebuf_width && y + 1 == framebuf_height)
    front_index.store(back_index, std::memory_order_release);
}

inline auto calc_delta(const std::chrono::steady_clock::time_point &start) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

void SDL3Frontend::queue_audio_samples(const float *samples,
                                       std::size_t sample_count) {
  if (!samples || sample_count == 0)
    return;

  std::scoped_lock lock(audio_mutex);
  if (!audio_device || !audio_stream)
    return;

  constexpr std::size_t max_queue_bytes = 48000 * 2 * sizeof(float); // ~1s
  const int queued = SDL_GetAudioStreamQueued(audio_stream);

  if (queued < 0) {
    SDL_ClearAudioStream(audio_stream);
    return;
  }
  if ((std::size_t)queued > max_queue_bytes)
    return;

  const int byte_count = (int)(sample_count * sizeof(float));
  if (!SDL_PutAudioStreamData(audio_stream, samples, byte_count)) {
    SDL_ClearAudioStream(audio_stream);
  }
}

void SDL3Frontend::clear(std::uint32_t c) {
  for (int i = 0; i < framebuf_width * framebuf_height; ++i)
    for (auto &buffer : framebuffers)
      buffer[i] = c;
}

void SDL3Frontend::start() {
  std::optional<std::string> bios_path{std::nullopt};
  std::string rom_path{};

  clear(black);
  while (running.load()) [[likely]] {

    /* Handle cart re-insertion */
    if (consume_load_rom_request(rom_path)) {
      join_emu_thread_if_running();

      /* Attempt to load cartridge, if it fails thread doesn't start */
      try {
        cart cart_ctx = load_cart_fs(rom_path.c_str());
        emulation_thread = std::jthread(&SDL3Frontend::emulation_thread_fn,
                                        this, cart_ctx, bios_path);
        set_status_message(std::format("Loaded ROM: {}", rom_path));
      } catch (std::exception &e) {
        set_status_message(std::format("Failed to load ROM: {}", e.what()));
      }
    }

    /* Handle BIOS selection, won't take effect until ROM re-inserted */
    consume_load_bios_request(bios_path);
    poll_events();
    present_ui();
  }

  /* Kill emulation thread */
  emulation_thread.request_stop();
  join_emu_thread_if_running();
}

std::tuple<ImVec2, ImVec2> SDL3Frontend::calc_winsize_bounds() const {
  const float display_w = ImGui::GetIO().DisplaySize.x;
  const float display_h = ImGui::GetIO().DisplaySize.y;
  return std::make_tuple(ImVec2((float)display_w, (float)display_h),
                         ImVec2(400.0f, 250.0f));
}

/*
 * ============================================================
 *  General User Interface Helpers
 * ============================================================
 */

void SDL3Frontend::build_main_menu_bar(ImVec2 max_size, ImVec2 min_size) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Load ROM..."))
        ImGuiFileDialog::Instance()->OpenDialog(
            "RomFileDialog", "Choose a ROM file", rom_filters, rom_sel_conf);

      if (ImGui::BeginMenu("Open Recent")) {
        if (settings.recent_roms.empty()) {
          ImGui::MenuItem("(No recent files)", nullptr, false, false);
        } else {
          for (const auto &path : settings.recent_roms) {
            // Display full path for clarity, alternatively we can use
            // std::filesystem::path(path).filename().string().c_str() for short
            // names
            if (ImGui::MenuItem(
                    std::filesystem::path(path).filename().string().c_str())) {
              ui_state.rom_path = path;
              ui_state.request_load_rom = true;
            }
          }
        }
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Quit"))
        running = false;
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Options")) {
      if (ImGui::MenuItem("Emulator Settings"))
        ui_state.show_settings_window = true;
      if (ImGui::MenuItem("Select BIOS"))
        ImGuiFileDialog::Instance()->OpenDialog(
            "BiosFileDialog", "Choose a BIN file", bios_filters, bios_sel_conf);
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
      if (ImGui::MenuItem("Open Debugger"))
        ui_state.show_debug_window = true;
      if (ImGui::MenuItem("Edit Breakpoints"))
        ui_state.show_breakpoints_window = true;
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void SDL3Frontend::build_rom_selection_dialog(ImVec2 max_size,
                                              ImVec2 min_size) {
  if (ImGuiFileDialog::Instance()->Display(
          "RomFileDialog", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      ui_state.rom_path = ImGuiFileDialog::Instance()->GetFilePathName();
      ui_state.request_load_rom = true;
    }
    ImGuiFileDialog::Instance()->Close();
    ui_state.show_load_window = false;
  }
}

void SDL3Frontend::build_bios_selection_dialog(ImVec2 max_size,
                                               ImVec2 min_size) {
  if (ImGuiFileDialog::Instance()->Display(
          "BiosFileDialog", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      ui_state.bios_path = ImGuiFileDialog::Instance()->GetFilePathName();
      ui_state.request_load_bios = true;
    }
    ImGuiFileDialog::Instance()->Close();
    ui_state.show_load_window = false;
  }
}

void SDL3Frontend::build_settings_dialog() {
  if (ui_state.show_settings_window) {
    ImGui::Begin("Settings", &ui_state.show_settings_window);
    ImGui::Checkbox("Fast forward", &ui_state.fast_forward);
    ImGui::Checkbox("Force DMG monochrome", &settings.force_mono_dmg);
    ImGui::SeparatorText("Audio");

    // Volume slider
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderFloat("Volume", &settings.volume, 0.0f, 1.5f, "%.2f")) {
      std::scoped_lock audio_lock(audio_mutex);
      if (audio_stream) {
        SDL_SetAudioStreamGain(audio_stream, settings.volume);
      }
    }

    // Output device dropdown
    if (ui_state.output_device_names.empty()) {
      refresh_output_devices();
      ui_state.output_device_index = 0;
    }
    ImGui::SetNextItemWidth(260.0f);

    std::vector<const char *> items;
    items.reserve(ui_state.output_device_names.size());
    for (auto &s : ui_state.output_device_names)
      items.push_back(s.c_str());

    int old_audio_idx = ui_state.output_device_index;
    if (ImGui::Combo("Output device", &ui_state.output_device_index,
                     items.data(), static_cast<int>(items.size()))) {
      if (!switch_output_device_by_index(ui_state.output_device_index)) {
        ui_state.output_device_index = old_audio_idx; // revert on failure
        switch_output_device_by_index(old_audio_idx);
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
      refresh_output_devices();
      ui_state.output_device_index =
          std::min(ui_state.output_device_index,
                   static_cast<int>(ui_state.output_device_names.size()) - 1);
    }

    ImGui::SeparatorText("Keybinds");
    // --- Preset dropdown ---
    {
      // Build an array of names for ImGui::Combo
      static std::array<const char *, kPresets.size()> preset_names{};
      static bool preset_names_init = false;
      if (!preset_names_init) {
        for (size_t i = 0; i < kPresets.size(); ++i)
          preset_names[i] = kPresets[i].name;
        preset_names_init = true;
      }

      int old_idx = settings.keybind_preset_index;
      ImGui::SetNextItemWidth(100.0f);
      if (ImGui::Combo("Preset", &settings.keybind_preset_index,
                       preset_names.data(), preset_names.size())) {
        // Only apply immediately if not currently rebinding
        if (ui_state.waiting_for_bind < 0) {
          apply_keybind_preset(settings.keybinds,
                               settings.keybind_preset_index);
        } else {
          // revert change while waiting for bind
          settings.keybind_preset_index = old_idx;
        }
      }
    }
    ImGui::Spacing();

    static constexpr std::array<const char *, KCount> keybind_labels{
        "Right", "Left", "Up", "Down", "A", "B", "Select", "Start"};
    for (std::size_t i = 0; i < keybind_labels.size(); ++i) {
      ImGui::Text("%s", keybind_labels[i]);
      ImGui::SameLine(120.0f);

      const bool waiting = (ui_state.waiting_for_bind == static_cast<int>(i));
      std::string button_label =
          waiting ? "Press a key..."
                  : (std::string("Bind##") + keybind_labels[i]);
      if (ImGui::Button(button_label.c_str()))
        ui_state.waiting_for_bind = static_cast<int>(i);

      ImGui::SameLine(240.0f);
      ImGui::Text("%s", SDL_GetKeyName(settings.keybinds[i]));
    }
    ImGui::End();
  }
}

bool SDL3Frontend::consume_load_bios_request(opt_string_t &bios_path) {
  std::lock_guard<std::mutex> lock(ui_mutex);
  if (!ui_state.request_load_bios)
    return false;

  /* Denote new BIOS path */
  ui_state.request_load_bios = false;
  bios_path = ui_state.bios_path;
  return true;
}

bool SDL3Frontend::consume_load_rom_request(std::string &rom_path) {
  std::lock_guard<std::mutex> lock(ui_mutex);
  if (!ui_state.request_load_rom)
    return false;

  /* Denote new cartridge path */
  ui_state.request_load_rom = false;
  rom_path = ui_state.rom_path;

  /* Update recent ROMs and last used directory */
  const auto new_rom_path = fs::path(rom_path).parent_path().string();
  rom_sel_conf.path = new_rom_path;
  settings.rom_dir = new_rom_path;
  settings.add_recent_rom(rom_path);
  settings.save();
  return true;
}

void SDL3Frontend::set_status_message(std::string message) {
  std::lock_guard<std::mutex> lock(ui_mutex);
  ui_state.status_message = std::move(message);
}

void SDL3Frontend::present_ui() {
  using namespace std::chrono;

  const std::uint32_t *pixels = get_front_buffer();
  int window_w{}, window_h{};
  uint32_t *texturePixels{};
  int pitch{};

  SDL_LockTexture(texture, nullptr, reinterpret_cast<void **>(&texturePixels),
                  &pitch);

  pitch /= sizeof(uint32_t);
  for (int y = 0; y < framebuf_height; ++y)
    for (int x = 0; x < framebuf_width; ++x) {
      const auto c = format_pixel_data(pixels[y * framebuf_width + x]);
      texturePixels[y * pitch + x] = c;
    }
  SDL_UnlockTexture(texture);

  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  build_ui();
  ImGui::Render();
  SDL_RenderClear(renderer);

  /* Need to account for bar consuming space for top few pixels */
  SDL_GetWindowSize(window, &window_w, &window_h);
  const float menu_bar_h = ImGui::GetFrameHeight();
  SDL_FRect dst_rect{0.0f,       // x
                     menu_bar_h, // y offset by menu bar
                     float(window_w), float(window_h - menu_bar_h)};

  SDL_RenderTexture(renderer, texture, nullptr, &dst_rect);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
  SDL_RenderPresent(renderer);
}

void SDL3Frontend::build_ui() {
  std::lock_guard<std::mutex> lock(ui_mutex);
  const auto [max_size, min_size] = calc_winsize_bounds();
  build_main_menu_bar(max_size, min_size);
  build_rom_selection_dialog(max_size, min_size);
  build_bios_selection_dialog(max_size, min_size);
  build_settings_dialog();
  build_debug_dialog();
  build_breakpoint_dialog();
  emu_state.fast_forward.store(ui_state.fast_forward);
}

void SDL3Frontend::build_breakpoint_dialog() {
  if (ui_state.show_breakpoints_window) {
    std::lock_guard<std::mutex> lock(dbg_mutex);
    ImGui::Begin("Breakoints", &ui_state.show_breakpoints_window);
    ImGui::SeparatorText("Breakpoints");
    auto &debugger = gbc_->get_debugger();

    if (!debugger.has_value()) {
      ImGui::Text("Debugger not configured");
      ImGui::End();
      return;
    }

    const auto bps = debugger->get_breakpoints();
    for (const auto &[addr, bp] : bps) {
      ImGui::PushID(addr);

      ImGui::Text("%s", bp.to_string().c_str());
      ImGui::SameLine();
      if (ImGui::Button("Edit")) {
        bp_prompt.execute = bp.has_flag(Debug::BRK_ADDRESS_EXECUTED);
        bp_prompt.read = bp.has_flag(Debug::BRK_ADDRESS_READ);
        bp_prompt.write = bp.has_flag(Debug::BRK_ADDRESS_WRITTEN);
        bp_prompt.addr = addr;
        bp_prompt.show = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove"))
        debugger->breakpoint_del(addr);
      ImGui::PopID();
    }
    if (ImGui::Button("Add"))
      bp_prompt.show = true;

    if (bp_prompt.show) {
      ImGui::OpenPopup("Configure breakpoint");
      build_config_breakpoint_dialog();
    }
    ImGui::End();
  }
}

/*
 * ============================================================
 *  Debugger User Interface Helpers
 * ============================================================
 */

void SDL3Frontend::build_config_breakpoint_dialog() {
  if (ImGui::BeginPopupModal("Configure breakpoint", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {

    ImGui::InputScalar("Address", ImGuiDataType_U16, &bp_prompt.addr, nullptr,
                       nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::Checkbox("Exec", &bp_prompt.execute);
    ImGui::SameLine();
    ImGui::Checkbox("Read", &bp_prompt.read);
    ImGui::SameLine();
    ImGui::Checkbox("Write", &bp_prompt.write);
    ImGui::Separator();

    if (ImGui::Button("OK", ImVec2(120, 0))) {
      Debug::BreakReason reason{};
      if (bp_prompt.execute)
        reason = reason | Debug::BRK_ADDRESS_EXECUTED;
      if (bp_prompt.write)
        reason = reason | Debug::BRK_ADDRESS_WRITTEN;
      if (bp_prompt.read)
        reason = reason | Debug::BRK_ADDRESS_READ;
      gbc_->get_debugger()->breakpoint_add(bp_prompt.addr, reason);
      ImGui::CloseCurrentPopup();
      bp_prompt.show = false;
    }
    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
      bp_prompt.show = false;
    }

    ImGui::EndPopup();
  }
}

void SDL3Frontend::read_system_dbg_state() {
  dbg_state.sys_state = Debug::to_string(gbc_->get_sys());
  dbg_state.cpu_state = Debug::to_string(gbc_->get_cpu()->get_state());
  dbg_state.disasm = gbc_->get_cpu()->disasm();

  const InterruptBits *const ie_reg = dynamic_cast<InterruptBits *>(
      gbc_->get_bus()->get_mmio(IORegisterMapping::MMIO_INT_ENABLE));
  const InterruptBits *const if_reg = dynamic_cast<InterruptBits *>(
      gbc_->get_bus()->get_mmio(IORegisterMapping::MMIO_INT_FLAGS));
  dbg_state.ie_state = Debug::to_string(*ie_reg);
  dbg_state.if_state = Debug::to_string(*if_reg);
}

void SDL3Frontend::build_debug_dialog() {
  if (ui_state.show_debug_window) {
    ImGui::Begin("Debug", &ui_state.show_debug_window);

    ImGui::SeparatorText("System State");
    ImGui::Text("Disassembly: %s", dbg_state.disasm.c_str());
    ImGui::Text(" %s", dbg_state.sys_state.c_str());

    ImGui::SeparatorText("Processor State");
    ImGui::Text("%s", dbg_state.cpu_state.c_str());
    ImGui::SameLine();
    ImGui::Text("IF: %s", dbg_state.if_state.c_str());
    ImGui::SameLine();
    ImGui::Text("IE: %s", dbg_state.ie_state.c_str());

    ImGui::SeparatorText("Control Flow");
    if (ImGui::Button("Break")) {
      std::lock_guard<std::mutex> lock(dbg_mutex);
      dbg_state.reason = Debug::BRK_STOPPED_BY_UI;
      dbg_state.stopped = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Instruction")) {
      {
        std::lock_guard<std::mutex> lock(dbg_mutex);
        dbg_state.reason = Debug::BRK_STEP_INSTRUCTION;
        dbg_state.stopped = false;
      }
      dbg_cv.notify_one();
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Scanline")) {
      {
        std::lock_guard<std::mutex> lock(dbg_mutex);
        dbg_state.reason = Debug::BRK_STEP_SCANLINE;
        dbg_state.stopped = false;
      }
      dbg_cv.notify_one();
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Frame")) {
      {
        std::lock_guard<std::mutex> lock(dbg_mutex);
        dbg_state.reason = Debug::BRK_STEP_FRAME;
        dbg_state.stopped = false;
      }
      dbg_cv.notify_one();
    }
    ImGui::SameLine();
    if (ImGui::Button("Continue")) {
      {
        std::lock_guard<std::mutex> lock(dbg_mutex);
        dbg_state.reason = Debug::BRK_CONTINUE;
        dbg_state.stopped = false;
      }
      dbg_cv.notify_one();
    }

    ImGui::End();
  }
}

const std::uint32_t SDL3Frontend::format_pixel_data(std::uint32_t px) const {
  constexpr std::uint32_t alpha_mask = 0xFF000000;
  /* We are abusing the alpha bits to store DMG color palette indecies CGB mode
   * will always be colored so the bits as are just returned w alpha bits set */

  if (!emu_state.is_cgb.load() && settings.force_mono_dmg) {
    const byte_t mono_pal_idx = static_cast<byte_t>((px >> 24) & 0xFF);
    return get_mono_color(mono_pal_idx) | alpha_mask;
  }
  return px | alpha_mask;
}

const std::uint32_t *SDL3Frontend::get_front_buffer() const {
  return framebuffers[front_index.load(std::memory_order_acquire)].get();
}

/*
 * ============================================================
 *  Input / Joypad Update Helpers
 * ============================================================
 */

void SDL3Frontend::apply_keybind_preset(
    std::array<SDL_Keycode, KCount> &keybinds, int preset_index) {
  if (preset_index < 0 || preset_index >= static_cast<int>(kPresets.size()))
    return;
  if (preset_index == kCustomPresetIndex)
    return; // don't clobber custom
  keybinds = kPresets[preset_index].keys;
}

void SDL3Frontend::update_button_state(const SDL_Keycode key,
                                       const bool pressed) {
  const byte_t mask = button_mask_for_key(key);
  if (mask == 0)
    return;

  byte_t current = input_state.buttons.load(std::memory_order_relaxed);
  if (pressed)
    current |= mask;
  else
    current &= static_cast<byte_t>(~mask);
  input_state.buttons.store(current, std::memory_order_relaxed);
}

byte_t SDL3Frontend::button_mask_for_key(const SDL_Keycode key) const {
  for (std::size_t i = 0; i < settings.keybinds.size(); ++i) {
    if (settings.keybinds[i] == key)
      return static_cast<byte_t>(button_order[i]);
  }
  return 0;
}

void SDL3Frontend::poll_events() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    ImGui_ImplSDL3_ProcessEvent(&e);
    if (e.type == SDL_EVENT_QUIT)
      running = false;
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
      ImGuiIO &io = ImGui::GetIO();
      if (e.type == SDL_EVENT_KEY_DOWN && ui_state.waiting_for_bind) {
        if (e.key.key != SDLK_ESCAPE)
          settings.keybinds[*ui_state.waiting_for_bind] = e.key.key;
        ui_state.waiting_for_bind.reset();
        // Any manual change -> Custom
        settings.keybind_preset_index = kCustomPresetIndex;
        continue;
      }
      if (io.WantCaptureKeyboard)
        continue;
      const bool pressed = (e.type == SDL_EVENT_KEY_DOWN);
      update_button_state(e.key.key, pressed);
    }
  }
}

/*
 * ============================================================
 *  Audio helpers
 * ============================================================
 */

bool SDL3Frontend::switch_output_device_by_index(int idx) {
  if (idx < 0 || idx >= static_cast<int>(ui_state.output_device_ids.size()))
    return false;

  const SDL_AudioDeviceID desired = ui_state.output_device_ids[idx];
  std::scoped_lock lock(audio_mutex);
  if (!audio_stream)
    return false;

  // Stop audio on old device
  SDL_ClearAudioStream(audio_stream);
  if (audio_device) {
    SDL_UnbindAudioStream(audio_stream);
    SDL_CloseAudioDevice(audio_device);
    audio_device = 0;
  }

  // Open new device (can be a physical device id or
  // SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
  audio_device = SDL_OpenAudioDevice(desired, &audio_spec);
  if (!audio_device) {
    set_status_message(SDL_GetError());
    return false;
  }
  if (!SDL_BindAudioStream(audio_device, audio_stream)) {
    set_status_message(SDL_GetError());
    SDL_CloseAudioDevice(audio_device);
    audio_device = 0;
    return false;
  }

  // Re-apply gain after rebinding
  SDL_SetAudioStreamGain(audio_stream, settings.volume);
  return true;
}

void SDL3Frontend::refresh_output_devices() {
  ui_state.output_device_ids.clear();
  ui_state.output_device_names.clear();

  // Slot 0: System default
  ui_state.output_device_ids.push_back(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
  ui_state.output_device_names.emplace_back("System default");

  int count = 0;
  SDL_AudioDeviceID *devs = SDL_GetAudioPlaybackDevices(&count);
  if (!devs) {
    set_status_message(SDL_GetError());
    return;
  }

  // SDL docs: returns a 0-terminated array; also provides count
  for (int i = 0; devs[i] != 0; ++i) {
    const SDL_AudioDeviceID id = devs[i];
    const char *name = SDL_GetAudioDeviceName(id); // human-readable name
    ui_state.output_device_ids.push_back(id);
    ui_state.output_device_names.emplace_back(name ? name : "(unknown device)");
  }
  SDL_free(devs);
}

/*
 * ============================================================
 *  Emulation Thread Helpers
 * ============================================================
 */

void SDL3Frontend::emulation_thread_fn(std::stop_token st, cart c,
                                       std::optional<std::string> bios_path) {
  bool ff = false;
  clear(black);

  /* Reset visual and auditory components */
  front_index.store(0, std::memory_order_relaxed);
  SDL_ClearAudioStream(audio_stream);

  /* Re-instantiate emulator instance */
  gbc_ = bios_path.has_value()
             ? std::make_unique<GameBoyColor>(*this, bios_path.value())
             : std::make_unique<GameBoyColor>(*this);
  gbc_->insert_cartridge(c);

  /* Configure the debugger */
  auto on_brk_callback = [this, st]() {
    std::unique_lock<std::mutex> lock(dbg_mutex);
    dbg_state.stopped = true;
    read_system_dbg_state();

    dbg_cv.wait(lock, [this, st] { // Avoid polling loops
      return !dbg_state.stopped || st.stop_requested();
    });
    return st.stop_requested() ? Debug::BRK_CONTINUE : dbg_state.reason;
  };
  gbc_->configure_debugger(Debug::Debugger(on_brk_callback));

  /* Establish connection with button state */
  auto *joypad = dynamic_cast<Joypad::JOYP *>(
      gbc_->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  if (!joypad)
    throw std::logic_error("Failed to configure joypad input");

  /* Run emulation in real-time */
  while (!st.stop_requested()) [[likely]] {
    // Audio sync logic
    // Check how much audio is currently buffered
    const int queued_bytes = SDL_GetAudioStreamQueued(audio_stream);
    int queued_ms = (queued_bytes * 1000) / (sizeof(float) * 2 * 48000);

    // If we are ahead of the target (and not fast-forwarding), sleep briefly.
    // 1ms should be short enough to prevent underruns
    if (!ff && queued_ms > target_queue_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    /* Read input state and catch up with audio stream, the max_catchup_cycles
     * is ~17556 cycles (~4ms of emulated time) */
    joypad->set_state(input_state.buttons.load(std::memory_order_relaxed));
    for (auto i{0}; i < max_catchup_cycles; i++)
      gbc_->step();

    /* Update additional meta-data, avoid mutex acquisition */
    emu_state.is_cgb.store(gbc_->get_sys().cgb_mode);
    ff = emu_state.fast_forward.load();

    /* Update the fuck ass debugger */
    std::lock_guard<std::mutex> lock(dbg_mutex);
    if (dbg_state.stopped)
      gbc_->get_debugger()->request_stop(dbg_state.reason);
  }
}

void SDL3Frontend::join_emu_thread_if_running() {
  if (emulation_thread.joinable()) {
    emulation_thread.request_stop();
    {
      std::lock_guard<std::mutex> lock(dbg_mutex);
      dbg_state.stopped = false;
    }
    dbg_cv.notify_all();
    emulation_thread.join();
  }
}
