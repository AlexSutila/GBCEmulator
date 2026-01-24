#include "frontend/sdl3_frontend.hpp"
#include "SDL3/SDL_audio.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include "cart/cart.hpp"
#include "memory/mmio/dmg.hpp"
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
#include <stdexcept>
#include <thread>

static const char *filters =
    "ROM files (*.gb *.gbc){.gb,.gbc},All files (*.*){.*}";
static constexpr double cycles_per_audio_frame = 4'194'304.0 / 48'000.0;
static constexpr unsigned max_catchup_cycles = 70'224 / 4;
static constexpr unsigned target_queue_ms = 20;
static constexpr std::uint32_t black = 0xFF000000;

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
  settings_ = Settings::load();

  /* ImGui initialization */
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer))
    throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
  if (!ImGui_ImplSDLRenderer3_Init(renderer))
    throw std::runtime_error("Failed to initialize ImGui SDL renderer backend");

  config.path = ".";
  config.flags =
      ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ReadOnlyFileNameField;
  framebuffers[0] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  framebuffers[1] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  pixels_rendered = 0;
  running = true;
  clear();

  if (settings_.keybind_preset_index != kCustomPresetIndex) {
    ApplyPreset(settings_.keybinds, settings_.keybind_preset_index);
  }
}

SDL3Frontend::~SDL3Frontend() {
  // Save current settings to disk
  settings_.save();

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
  ++pixels_rendered;

  /* Frame is complete so swap frame buffers */
  if (pixels_rendered == framebuf_height * framebuf_width) {
    front_index.store(back_index, std::memory_order_release);
    pixels_rendered = 0;
  }
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
          settings_.keybinds[*ui_state.waiting_for_bind] = e.key.key;
        ui_state.waiting_for_bind.reset();
        // Any manual change -> Custom
        settings_.keybind_preset_index = kCustomPresetIndex;
        continue;
      }
      if (io.WantCaptureKeyboard)
        continue;
      const bool pressed = (e.type == SDL_EVENT_KEY_DOWN);
      update_button_state(e.key.key, pressed);
    }
  }
}

inline auto calc_delta(const std::chrono::steady_clock::time_point &start) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

void SDL3Frontend::present_ui() {
  using namespace std::chrono;

  const std::uint32_t *pixels = front_buffer();
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

void SDL3Frontend::clear(std::uint32_t c) {
  for (int i = 0; i < framebuf_width * framebuf_height; ++i)
    for (auto &buffer : framebuffers)
      buffer[i] = c;
}

void SDL3Frontend::queue_audio_samples(const float *samples,
                                       std::size_t sample_count) {
  if (!samples || sample_count == 0) return;

  std::scoped_lock lock(audio_mutex);
  if (!audio_device || !audio_stream) return;

  constexpr std::size_t max_queue_bytes = 48000 * 2 * sizeof(float); // ~1s
  const int queued = SDL_GetAudioStreamQueued(audio_stream);

  if (queued < 0) { SDL_ClearAudioStream(audio_stream); return; }
  if ((std::size_t)queued > max_queue_bytes) return;

  const int byte_count = (int)(sample_count * sizeof(float));
  if (!SDL_PutAudioStreamData(audio_stream, samples, byte_count)) {
    SDL_ClearAudioStream(audio_stream);
  }
}

void SDL3Frontend::refresh_output_devices() {
  ui_state.output_device_ids.clear();
  ui_state.output_device_names.clear();

  // Slot 0: System default
  ui_state.output_device_ids.push_back(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
  ui_state.output_device_names.emplace_back("System default");

  int count = 0;
  SDL_AudioDeviceID* devs = SDL_GetAudioPlaybackDevices(&count);
  if (!devs) {
    set_status_message(SDL_GetError());
    return;
  }

  // SDL docs: returns a 0-terminated array; also provides count
  for (int i = 0; devs[i] != 0; ++i) {
    const SDL_AudioDeviceID id = devs[i];
    const char* name = SDL_GetAudioDeviceName(id); // human-readable name
    ui_state.output_device_ids.push_back(id);
    ui_state.output_device_names.emplace_back(name ? name : "(unknown device)");
  }

  SDL_free(devs);
}

bool SDL3Frontend::switch_output_device_by_index(int idx) {
  if (idx < 0 || idx >= static_cast<int>(ui_state.output_device_ids.size())) return false;

  const SDL_AudioDeviceID desired = ui_state.output_device_ids[idx];

  std::scoped_lock lock(audio_mutex);

  if (!audio_stream) return false;

  // Stop audio on old device
  SDL_ClearAudioStream(audio_stream);

  if (audio_device) {
    SDL_UnbindAudioStream(audio_stream);
    SDL_CloseAudioDevice(audio_device);
    audio_device = 0;
  }

  // Open new device (can be a physical device id or SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
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
  SDL_SetAudioStreamGain(audio_stream, settings_.volume);
  return true;
}


bool SDL3Frontend::consume_load_request(std::string &rom_path) {
  std::lock_guard<std::mutex> lock(ui_mutex);
  if (!ui_state.request_load)
    return false;

  /* Denote new cartridge path */
  ui_state.request_load = false;
  rom_path = ui_state.rom_path;
  return true;
}

void SDL3Frontend::set_status_message(std::string message) {
  std::lock_guard<std::mutex> lock(ui_mutex);
  ui_state.status_message = std::move(message);
}

const int SDL3Frontend::calc_sync_cycles() const {
  int queued_bytes = SDL_GetAudioStreamQueued(audio_stream);
  queued_bytes = std::max(queued_bytes, 0); // Clamp to be non-negative

  const int bytes_per_frame = int(sizeof(float) * 2); // stereo float
  const int queued_frames = queued_bytes / bytes_per_frame;
  const int target_frames = (audio_spec.freq * target_queue_ms) / 1000;

  int delta_frames = target_frames - queued_frames;
  int cycles = delta_frames * cycles_per_audio_frame;
  return std::clamp(cycles, 0, int(max_catchup_cycles));
}

void SDL3Frontend::build_ui() {
  std::lock_guard<std::mutex> lock(ui_mutex);
  ImGuiIO &io = ImGui::GetIO();
  const float display_w = io.DisplaySize.x;
  const float display_h = io.DisplaySize.y;

  max_size = ImVec2((float)display_w, (float)display_h);
  min_size = ImVec2(400.0f, 250.0f);

  /* Main menu bar */
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Load ROM..."))
        ImGuiFileDialog::Instance()->OpenDialog(
            "RomFileDialog", "Choose a ROM file", filters, config);
      if (ImGui::MenuItem("Quit"))
        running = false;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings")) {
      if (ImGui::MenuItem("Emulator Settings"))
        ui_state.show_settings_window = true;
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  /* ROM selection dialog */
  if (ImGuiFileDialog::Instance()->Display(
          "RomFileDialog", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      ui_state.rom_path = ImGuiFileDialog::Instance()->GetFilePathName();
      ui_state.request_load = true;
    }
    ImGuiFileDialog::Instance()->Close();
    ui_state.show_load_window = false;
  }

  /* Settings dialog */
  if (ui_state.show_settings_window) {
    ImGui::Begin("Settings", &ui_state.show_settings_window);
    ImGui::Checkbox("Fast forward", &ui_state.fast_forward);
    ImGui::Checkbox("Force DMG monochrome", &settings_.force_mono_dmg);

    ImGui::SeparatorText("Audio");

    // Volume slider
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderFloat("Volume", &settings_.volume, 0.0f, 1.5f, "%.2f")) {
      std::scoped_lock audio_lock(audio_mutex);
      if (audio_stream) {
        SDL_SetAudioStreamGain(audio_stream, settings_.volume);
      }
    }

    // Output device dropdown
    if (ui_state.output_device_names.empty()) {
      refresh_output_devices();
      ui_state.output_device_index = 0;
    }

    ImGui::SetNextItemWidth(260.0f);

    std::vector<const char*> items;
    items.reserve(ui_state.output_device_names.size());
    for (auto& s : ui_state.output_device_names) items.push_back(s.c_str());

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
      ui_state.output_device_index = std::min(ui_state.output_device_index,
                                             static_cast<int>(ui_state.output_device_names.size()) - 1);
    }


    ImGui::SeparatorText("Keybinds");
    // --- Preset dropdown ---
    {
      // Build an array of names for ImGui::Combo
      static std::array<const char*, kPresets.size()> preset_names{};
      static bool preset_names_init = false;
      if (!preset_names_init) {
        for (size_t i = 0; i < kPresets.size(); ++i) preset_names[i] = kPresets[i].name;
        preset_names_init = true;
      }

      int old_idx = settings_.keybind_preset_index;
      ImGui::SetNextItemWidth(100.0f);
      if (ImGui::Combo("Preset", &settings_.keybind_preset_index,
                       preset_names.data(), preset_names.size())) {
        // Only apply immediately if not currently rebinding
        if (ui_state.waiting_for_bind < 0) {
          ApplyPreset(settings_.keybinds, settings_.keybind_preset_index);
        } else {
          // revert change while waiting for bind
          settings_.keybind_preset_index = old_idx;
        }
      }
    }

    ImGui::Spacing();

    static constexpr std::array<const char*, KCount> keybind_labels{
      "Right","Left","Up","Down","A","B","Select","Start"
    };

    for (std::size_t i = 0; i < keybind_labels.size(); ++i) {
      ImGui::Text("%s", keybind_labels[i]);
      ImGui::SameLine(120.0f);

      const bool waiting = (ui_state.waiting_for_bind == static_cast<int>(i));
      std::string button_label = waiting ? "Press a key..." : (std::string("Bind##") + keybind_labels[i]);

      if (ImGui::Button(button_label.c_str()))
        ui_state.waiting_for_bind = static_cast<int>(i);

      ImGui::SameLine(240.0f);
      ImGui::Text("%s", SDL_GetKeyName(settings_.keybinds[i]));
    }

    ImGui::End();
  }

  /* Update additional meta-data, avoid mutex acquisition */
  emu_state.fast_forward.store(ui_state.fast_forward);
}

const std::uint32_t SDL3Frontend::format_pixel_data(std::uint32_t px) const {
  constexpr std::uint32_t alpha_mask = 0xFF000000;
  /* We are abusing the alpha bits to store DMG color palette indecies */
  if (!emu_state.is_cgb.load() && settings_.force_mono_dmg) {
    const byte_t mono_pal_idx = static_cast<byte_t>((px >> 24) & 0xFF);
    return get_mono_color(mono_pal_idx) | alpha_mask;
  }
  /* CGB mode will always be colored */
  return px | alpha_mask;
}

const std::uint32_t *SDL3Frontend::front_buffer() const {
  return framebuffers[front_index.load(std::memory_order_acquire)].get();
}

void SDL3Frontend::emulation_thread_fn(std::stop_token st, cart c) {
  bool ff = false;
  clear(black);

  /* Reset visual and auditory components */
  front_index.store(0, std::memory_order_relaxed);
  SDL_ClearAudioStream(audio_stream);

  /* Re-instantiate emulator instance */
  gbc_ = std::make_unique<GameBoyColor>(*this);
  gbc_->insert_cartridge(c);

  auto *joypad = dynamic_cast<Joypad::JOYP *>(
      gbc_->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  if (!joypad)
    throw std::logic_error("Failed to configure joypad input");

  /* Run emulation in real-time */
  while (!st.stop_requested()) [[likely]] {
    const int sync_cycles = calc_sync_cycles();

    /* Read input state and catch up with audio stream */
    joypad->set_state(input_state.buttons.load(std::memory_order_relaxed));
    for (auto i{0}; i < sync_cycles; i++)
      gbc_->step();

    /* Hint to the OS to let this thread sleep. This is better than manually
     * claculating a sleep period and explicitly making this thread sleep bc
     * it is possible (and more likely) to introduce jitter which will cause
     * small breaks in the audio that sound like pops and cracks. */
    if (!ff && sync_cycles == 0)
      std::this_thread::yield();

    /* Update additional meta-data, avoid mutex acquisition */
    emu_state.is_cgb.store(gbc_->is_cgb_mode());
    ff = emu_state.fast_forward.load();
  }
}

void SDL3Frontend::join_emu_thread_if_running() {
  if (emulation_thread.joinable()) {
    emulation_thread.request_stop();
    emulation_thread.join();
  }
}

byte_t SDL3Frontend::button_mask_for_key(const SDL_Keycode key) const {
  for (std::size_t i = 0; i < settings_.keybinds.size(); ++i) {
    if (settings_.keybinds[i] == key)
      return static_cast<byte_t>(button_order[i]);
  }
  return 0;
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

void SDL3Frontend::start() {
  std::string rom_path{};
  clear(black);

  while (running.load()) [[likely]] {
    /* Handle cart re-insertion */
    if (consume_load_request(rom_path)) {
      join_emu_thread_if_running();

      /* Attempt to load cartridge, if it fails thread doesn't start */
      try {
        cart loaded = load_cart_fs(rom_path.c_str());
        emulation_thread =
            std::jthread(&SDL3Frontend::emulation_thread_fn, this, loaded);
        set_status_message(std::format("Loaded ROM: {}", rom_path));
      } catch (std::exception &e) {
        set_status_message(std::format("Failed to load ROM: {}", e.what()));
      }
    }

    /* Shows ui in what ever state it is currently in */
    poll_events();
    present_ui();
  }

  /* Kill emulation thread */
  emulation_thread.request_stop();
  join_emu_thread_if_running();
}
