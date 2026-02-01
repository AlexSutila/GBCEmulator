#include "frontend/sdl3/frontend.hpp"

SDL3Frontend::SDL3Frontend() : host(framebuf_width, framebuf_height, scale) {
  host.init_audio();
  gui.init(host);
  SDL_AddEventWatch(reinterpret_cast<SDL_EventFilter>(event_watcher), this);
  framebuffers[0] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  framebuffers[1] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  running = true;
  clear(black);
}

SDL3Frontend::~SDL3Frontend() {
  SDL_RemoveEventWatch(reinterpret_cast<SDL_EventFilter>(event_watcher), this);
  gui.shutdown();
  // The rest of destruction is handled in SDLHost destructor,
  // which should be called automatically at this point
}

/* Required Frontend methods */
std::array<std::uint32_t, 160 * 144> SDL3Frontend::get_frame() {
  const int idx = front_index.load(std::memory_order_relaxed);
  std::array<std::uint32_t, 160 * 144> arr{};
  for (auto i{0}; i < framebuf_width * framebuf_height; i++)
    arr[idx] = framebuffers[idx][i];
  return arr;
}

void SDL3Frontend::put_pixel(const int x, const int y, const std::uint32_t c) {
  if (x < 0 || x >= framebuf_width || y < 0 || y >= framebuf_height)
    return;

  /* We perform double buffering to prevent screen tearing */
  const int back_index = 1 - front_index.load(std::memory_order_relaxed);
  framebuffers[back_index][y * framebuf_width + x] = c;

  /* Frame completion can be indicated by the fact that we are placing
   * the last pixel in the frame, so we need to swap buffers here. */
  if (x + 1 == framebuf_width && y + 1 == framebuf_height) {
    front_index.store(back_index, std::memory_order_release);
    emulated_frame_count.fetch_add(1, std::memory_order_relaxed);
  }
}

void SDL3Frontend::clear(const std::uint32_t c) {
  for (int i = 0; i < framebuf_width * framebuf_height; ++i)
    for (auto &buffer : framebuffers)
      buffer[i] = c;
}

void SDL3Frontend::queue_audio_samples(const float *samples,
                                       const size_t count) {
  host.queue_audio(samples, count);
}

void SDL3Frontend::start() {
  std::optional<std::string> bios_path = gui.get_settings_c().prev_bios_path.empty()?
                                            std::nullopt :
                                            std::make_optional(gui.get_settings_c().prev_bios_path);
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
      } catch (std::exception &e) {
        Logger::push(LogLevel::Warning, "ROM", "Failed to load ROM", e.what());
      }
    }

    /* Handle BIOS selection, won't take effect until ROM re-inserted */
    consume_load_bios_request(bios_path);
    process_events();
    render_frame();
  }

  /* Kill emulation thread */
  emulation_thread.request_stop();
  join_emu_thread_if_running();
}

/* Rendering */
const std::uint32_t *SDL3Frontend::get_front_buffer() const {
  return framebuffers[front_index.load(std::memory_order_acquire)].get();
}

/* Main loop helpers */
void SDL3Frontend::process_events() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT) {
      running = false;
      continue;
    }
    // If process_event returns true, the GUI "ate" the input (e.g. rebinding)
    if (gui.process_event(e, ui_state)) {
      continue;
    }
    // If we got here, the GUI didn't want it
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
      // Update the atomic input state for the emulator thread and handle
      // general frontend input
      const bool pressed = (e.type == SDL_EVENT_KEY_DOWN);
      handle_keypress(e.key.key, pressed);
    }
  }
}

void SDL3Frontend::render_frame() {
  // --- PHASE 1: PREPARE TEXTURE ---
  // 1. Get the raw buffer from the emulator thread
  const std::uint32_t *raw_pixels = get_front_buffer();
  // 2. Send to GPU
  host.update_texture(raw_pixels, 160, 144, is_cgb,
                      gui.get_settings_c().force_mono_dmg);

  // 3. FPS Calculation
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - last_fps_check).count();

  // Update FPS readout every 500ms
  if (elapsed_ms >= 500) {
    const uint64_t current_count = emulated_frame_count.load(std::memory_order_relaxed);
    const uint64_t frames = current_count - last_frame_count;
    ui_state.current_fps = static_cast<double>(frames) * 1000 / static_cast<double>(elapsed_ms);

    last_frame_count = current_count;
    last_fps_check = now;
  }

  // --- PHASE 2: UI COMPOSITION ---
  // 1. Start the ImGui frame
  GbcImGui::new_frame();
  // 2. Build the UI Windows
  {
    std::lock_guard lock(ui_mutex);
    gui.render(ui_state, host);
  }
  fast_forward = ui_state.fast_forward;
  if (ui_state.request_quit) {
    ui_state.request_quit = false;
    running = false;
  }
  // 3. Build debugger windows (if active)
  if (ui_state.show_debug || ui_state.show_breakpoints) {
    debugger.render(ui_state, gbc);
  }
  // 4. Finalize ImGui frame
  GbcImGui::end_frame();

  // --- PHASE 3: DRAW TO SCREEN ---
  // 1. Clear background
  host.clear_screen();
  // 2. Draw the Emulator Output
  host.draw_texture(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
  // 3. Draw the ImGui Overlay
  host.draw_overlay(ImGui::GetDrawData());
  // 4. Swap buffers
  host.present();
}

void SDL3Frontend::emulation_thread_fn(const std::stop_token &st, const cart &c,
                                       const std::optional<std::string> &bios) {
  bool ff = false;
  clear(black);

  /* Reset visual and auditory components */
  front_index.store(0, std::memory_order_relaxed);
  host.clear_audio_stream();

  /* Re-instantiate emulator instance */
  if (bios.has_value()) {
    try {
      auto bios_rom = BootROM(bios.value());
      gbc = std::make_unique<GameBoyColor>(*this, bios_rom);
    } catch (std::runtime_error &e) {
      Logger::push(LogLevel::Warning, "BIOS", "Failed to load BIOS", e.what());
      gui.get_settings().prev_bios_path = "";
      gbc = std::make_unique<GameBoyColor>(*this);
    }
  } else {
    gbc = std::make_unique<GameBoyColor>(*this);
  }
  gbc->insert_cartridge(c);
  auto callback = [this, st]() -> Debug::BreakReason {
    return debugger.on_breakpoint(st, gbc);
  };
  gbc->configure_debugger(Debug::Debugger(callback));

  /* Establish connection with button state */
  auto *joypad = dynamic_cast<Joypad::JOYP *>(
      gbc->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  if (!joypad)
    throw std::logic_error("Failed to configure joypad input");

  /* Run emulation in real-time */
  while (!st.stop_requested()) [[likely]] {
    // Audio sync logic
    // Check how much audio is currently buffered
    const int queued_bytes = host.get_queued_audio_bytes();

    // If we are ahead of the target (and not fast-forwarding), sleep briefly.
    // 1ms should be short enough to prevent underruns
    if (const int queued_ms =
            static_cast<int>(queued_bytes * 1000 / (sizeof(float) * 2 * 48000));
        !ff && queued_ms > target_queue_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    /* Read input state and catch up with audio stream, the max_catchup_cycles
     * is ~17556 cycles (~4ms of emulated time) */
    joypad->set_state(input_state.buttons.load(std::memory_order_relaxed));
    for (auto i{0}; i < max_catchup_cycles; i++)
      gbc->step();

    /* Update additional meta-data, avoid mutex acquisition */
    is_cgb.store(gbc->get_sys().cgb_mode);
    ff = fast_forward.load();

    /* Update the fuck ass debugger */
    debugger.forward_stop(gbc);
  }
}

void SDL3Frontend::join_emu_thread_if_running() {
  if (emulation_thread.joinable()) {
    emulation_thread.request_stop();
    debugger.request_stop();
    emulation_thread.join();
  }
}

void SDL3Frontend::handle_keypress(const SDL_Keycode key, const bool pressed) {
  // Emulator input
  if (const byte_t mask = button_mask_for_key(key); mask != 0) {
    byte_t current = input_state.buttons.load(std::memory_order_relaxed);
    if (pressed)
      current |= mask;
    else
      current &= static_cast<byte_t>(~mask);
    input_state.buttons.store(current, std::memory_order_relaxed);
  }

  // Frontend input
  const auto &binds = gui.get_settings_c().general_keybinds;
  auto &settings = gui.get_settings();

  // FF toggle
  if (pressed && key == binds[0])
    ui_state.fast_forward = !ui_state.fast_forward;
  // FF Hold (overrides toggle)
  if (key == binds[1])
    ui_state.fast_forward = pressed;
  // Volume up
  if (pressed && key == binds[2]) {
    settings.volume = std::min(1.5f, settings.volume + 0.05f);
    host.set_volume(settings.volume);
  }
  // Volume Down
  if (pressed && key == binds[3]) {
    settings.volume = std::max(0.0f, settings.volume - 0.05f);
    host.set_volume(settings.volume);
  }
  // Monochrome
  if (pressed && key == binds[4])
    settings.force_mono_dmg = !settings.force_mono_dmg;
}

byte_t SDL3Frontend::button_mask_for_key(const SDL_Keycode key) const {
  for (std::size_t i = 0; i < gui.get_settings_c().keybinds.size(); ++i) {
    if (gui.get_settings_c().keybinds[i] == key)
      return static_cast<byte_t>(button_order[i]);
  }
  return 0;
}

/* Below functions uses ui_mutex */
/* ROM/file loading */
bool SDL3Frontend::consume_load_rom_request(std::string &rom_path) {
  std::lock_guard lock(ui_mutex);
  if (!ui_state.request_load_rom)
    return false;

  /* Denote new cartridge path */
  ui_state.request_load_rom = false;
  rom_path = ui_state.load_rom_path;

  gui.update_rom_path(rom_path);
  return true;
}

bool SDL3Frontend::consume_load_bios_request(
    std::optional<std::string> &bios_path) {
  std::lock_guard lock(ui_mutex);
  if (!ui_state.request_load_bios)
    return false;

  /* Denote new BIOS path */
  ui_state.request_load_bios = false;
  bios_path = ui_state.load_bios_path;

  gui.update_bios_path(ui_state.load_bios_path);
  return true;
}

bool SDLCALL SDL3Frontend::event_watcher(void* userdata, const SDL_Event* event) {
  if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
      event->type == SDL_EVENT_WINDOW_MOVED ||
      event->type == SDL_EVENT_WINDOW_EXPOSED) {

    static auto last_draw = std::chrono::steady_clock::now();

    // Force a frame update immediately
    // When the main loop is blocked during windows resizing
    if (const auto now = std::chrono::steady_clock::now();
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw).count() >= 16) {
      auto* self = static_cast<SDL3Frontend*>(userdata);
      self->render_frame();
      last_draw = now;
    }
  }

  // Return true to allow the event to propagate to SDL_PollEvent queue
  return true;
}