#include "frontend/sdl3_frontend.hpp"
#include "SDL3/SDL_video.h"
#include "cart/cart.hpp"
#include "memory/mmio/joypad.hpp"
#include "ppu/palette.hpp"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <chrono>
#include <cstdint>
#include <exception>
#include <imgui.h>
#include <memory>
#include <misc/cpp/imgui_stdlib.h>
#include <stdexcept>
#include <thread>

static const char *filters =
    "GBC ROM files (*.gb *.gbc){.gb,.gbc},All files (*.*){.*}";
static constexpr std::uint32_t black = 0xFF000000;

constexpr std::chrono::nanoseconds wait_sync_time_ns(unsigned sync_cycles) {
  constexpr std::uint64_t t_cycle_hz = 4'194'304;

  // ns = cycles * 1e9 / Hz
  return std::chrono::nanoseconds{(sync_cycles * 1'000'000'000ull) /
                                  t_cycle_hz};
}

SDL3Frontend::SDL3Frontend() : Frontend() {
  if (!SDL_Init(SDL_INIT_VIDEO))
    throw std::runtime_error(SDL_GetError());

  window = SDL_CreateWindow("GBC", framebuf_width * scale,
                            framebuf_height * scale, SDL_WINDOW_RESIZABLE);
  if (!window)
    throw std::runtime_error(SDL_GetError());

  renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer)
    throw std::runtime_error(SDL_GetError());
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, framebuf_width,
                              framebuf_height);
  if (!texture)
    throw std::runtime_error(SDL_GetError());

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
}

SDL3Frontend::~SDL3Frontend() {
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
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
          keybinds[*ui_state.waiting_for_bind] = e.key.key;
        ui_state.waiting_for_bind.reset();
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
    ImGui::Checkbox("Force DMG monochrome", &ui_state.force_mono_dmg);
    ImGui::SeparatorText("Keybinds");
    static constexpr std::array<const char *, 8> keybind_labels{
      "Right", "Left", "Up", "Down", "A", "B", "Select", "Start"};
    for (std::size_t i = 0; i < keybind_labels.size(); ++i) {
      ImGui::Text("%s", keybind_labels[i]);
      ImGui::SameLine(120.0f);
      const bool waiting = ui_state.waiting_for_bind == i;
      std::string button_label =
          waiting ? "Press a key..." : std::string("Bind##") + keybind_labels[i];
      if (ImGui::Button(button_label.c_str()))
        ui_state.waiting_for_bind = i;
      ImGui::SameLine(240.0f);
      ImGui::Text("%s", SDL_GetKeyName(keybinds[i]));
    }
    ImGui::End();
  }

  /* Update additional meta-data, avoid mutex acquisition */
  emu_state.fast_forward.store(ui_state.fast_forward);
}

const std::uint32_t SDL3Frontend::format_pixel_data(std::uint32_t px) const {
  constexpr std::uint32_t alpha_mask = 0xFF000000;
  /* We are abusing the alpha bits to store DMG color palette indecies */
  if (!emu_state.is_cgb.load() && ui_state.force_mono_dmg) {
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
  using steady_clk = std::chrono::steady_clock;
  using ns = std::chrono::nanoseconds;
  bool ff = false;

  constexpr unsigned sync_cycles = 10'000; // T-cycles
  constexpr ns target_step_time = wait_sync_time_ns(sync_cycles);
  clear(black);

  /* Re-instantiate emulator instance */
  gbc_ = std::make_unique<GameBoyColor>(*this);
  gbc_->insert_cartridge(c);

  auto *joypad = dynamic_cast<Joypad::JOYP *>(
    gbc_->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  if (!joypad)
    throw std::logic_error("Failed to configure joypad input");

  /* Run emulation in real-time */
  while (!st.stop_requested()) [[likely]] {
    const auto beg = steady_clk::now();
    joypad->set_state(input_state.buttons.load(std::memory_order_relaxed));
    for (unsigned i = 0; i < sync_cycles; i++)
      gbc_->step();

    /* Synchronize with real-time */
    const auto elapsed = steady_clk::now() - beg;
    if (elapsed < target_step_time && !ff)
      std::this_thread::sleep_for(target_step_time - elapsed);

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
  for (std::size_t i = 0; i < keybinds.size(); ++i) {
    if (keybinds[i] == key)
      return static_cast<byte_t>(button_order[i]);
  }
  return 0;
}

void SDL3Frontend::update_button_state(const SDL_Keycode key, const bool pressed) {
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
