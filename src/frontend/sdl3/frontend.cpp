#include "frontend/sdl3/frontend.hpp"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_gamepad.h"
#include "debugger/print.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <picosha2.h>
#include <span>

namespace {
std::int64_t steady_now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

std::string strip_colons(const std::string &path) {
  if (const auto i = path.find(" ::"); i != std::string::npos) {
    return path.substr(0, i);
  }
  return path;
}

std::string sha256_hex(const std::span<const byte_t> data) {
  std::string out;
  picosha2::hash256_hex_string(data.begin(), data.end(), out);
  return out;
}
} // namespace

SDL3Frontend::SDL3Frontend() : host(framebuf_width, framebuf_height, scale) {
  host.init_audio();
  gui.init(host);
  const auto bar_height_px =
      static_cast<int>(std::ceil(ImGui::GetFrameHeight()));
  SDL_SetWindowSize(host.get_window(), framebuf_width * scale,
                    framebuf_height * scale + bar_height_px * 2);
  debugger.init(host);
  SDL_AddEventWatch(reinterpret_cast<SDL_EventFilter>(event_watcher), this);
  framebuffers[0] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  framebuffers[1] =
      std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  running = true;
  SDL3Frontend::clear(black);

  std::error_code ec;
  tmp_root = std::filesystem::temp_directory_path(ec) / "tmp";
  std::filesystem::create_directories(tmp_root, ec);
  debugger.update_state_from_core(gbc);

  // We have to update this manually once because the emulation loop has not
  // started yet to do it for us. If we don't do this, we may end up seeing a
  // garbage value being used for the initial screen color.
  const std::uint32_t *raw_pixels = get_front_buffer();
  host.update_texture(raw_pixels, 160, 144, false, false);
}

SDL3Frontend::~SDL3Frontend() {
  SDL_RemoveEventWatch(reinterpret_cast<SDL_EventFilter>(event_watcher), this);
  release_savestate_textures_locked();
  gui.shutdown();
  // The rest of destruction is handled in SDLHost destructor,
  // which should be called automatically at this point
}

/* Required Frontend methods */
std::array<std::uint32_t, 160 * 144> SDL3Frontend::get_frame() {
  const int idx = front_index.load(std::memory_order_relaxed);
  std::array<std::uint32_t, 160 * 144> arr{};
  for (auto i{0}; i < framebuf_width * framebuf_height; i++)
    arr[i] = framebuffers[idx][i];
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
    video_dirty.store(true, std::memory_order_release);
  }
}

void SDL3Frontend::clear(const std::uint32_t c) {
  for (int i = 0; i < framebuf_width * framebuf_height; ++i)
    for (auto &buffer : framebuffers)
      buffer[i] = c;
  video_dirty.store(true, std::memory_order_relaxed);
}

void SDL3Frontend::queue_audio_samples(const float *samples,
                                       const size_t count) {
  host.queue_audio(samples, count);
}

void SDL3Frontend::start() {
  auto start_emulation =
      [this](cart cart_ctx, const std::string &display_label,
             const std::string &rom_hash,
             const std::optional<std::string> &bios_path,
             const std::optional<std::filesystem::path> &initial_save_path) {
        active_rom_hash = rom_hash;
        emulation_thread = std::jthread(
            std::bind_front(&SDL3Frontend::emulation_thread_fn, this), cart_ctx,
            bios_path, initial_save_path);
        {
          std::lock_guard lock(ui_mutex);
          ui_state.load_rom_path = display_label;
          ui_state.load_rom_name = cart_ctx.header.title();
          ui_state.cart_info = Debug::describe_cart(cart_ctx);
        }
        gui.update_rom_path(strip_colons(display_label));
      };

  std::optional<std::string> bios_path =
      gui.get_settings_c().prev_bios_path.empty()
          ? std::nullopt
          : std::make_optional(gui.get_settings_c().prev_bios_path);
  clear(black);

  std::string rom_source{};
  while (running.load()) {
    if (consume_load_rom_request(rom_source)) {
      join_emu_thread_if_running();
      reset_savestate_context();
      process_pending_save();
      active_rom_hash.clear();
      start_rom_io_job(rom_source);
    }
    std::string rom_on_disk;
    if (std::string display_label;
        consume_rom_io_result(rom_on_disk, display_label)) {
      try {
        cart cart_ctx = load_cart_fs(rom_on_disk.c_str());
        const std::string rom_hash = sha256_hex(cart_ctx.rom_span());
        setup_save_context(cart_ctx, display_label, rom_hash);
        start_emulation(std::move(cart_ctx), display_label, rom_hash, bios_path,
                        active_save_path);
      } catch (std::exception &e) {
        Logger::push(LogLevel::Warning, "ROM", "Failed to load ROM", e.what());
      }
    }

    /* Handle BIOS selection, won't take effect until ROM re-inserted */
    consume_load_bios_request(bios_path);
    process_events();
    process_pending_save();
    // Don't force a redraw if the watcher just forced it
    const auto now_ns = steady_now_ns();
    if (const auto last_forced =
            last_forced_redraw_ns.load(std::memory_order_relaxed);
        now_ns - last_forced > 2'000'000) // ~2ms
      render_frame();
  }

  /* Kill emulation thread */
  emulation_thread.request_stop();
  join_emu_thread_if_running();
  process_pending_save();
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

    bool consumed = false;
    // DPI scale change triggers SDL_SetWindowSize() inside gui.process_event(),
    // which can synchronously generate window events that hit our event watcher
    // Do NOT hold ui_mutex here, or we can deadlock
    if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
      consumed = gui.process_event(e, ui_state);
    } else {
      std::lock_guard lock(ui_mutex);
      consumed = gui.process_event(e, ui_state);
    }
    if (consumed)
      continue;

    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
      const bool pressed = (e.type == SDL_EVENT_KEY_DOWN);
      handle_keypress(e.key.key, pressed);
    } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
               e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
      const bool pressed = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
      handle_controller_press((SDL_GamepadButton)e.gbutton.button, pressed);
    }
    if (e.type == SDL_EVENT_DROP_FILE || e.type == SDL_EVENT_DROP_TEXT) {
      handle_drop(e);
    }
    if (e.type == SDL_EVENT_DROP_FILE || e.type == SDL_EVENT_DROP_TEXT) {
      handle_drop(e);
    }
  }
}

void SDL3Frontend::render_frame() {
  // --- PHASE 1: PREPARE TEXTURE ---
  // 0. Check if we need to update the texture (avoid redundant GPU uploads)
  if (render_guard.test_and_set(std::memory_order_acquire))
    return;
  struct Guard {
    std::atomic_flag &f;
    ~Guard() { f.clear(std::memory_order_release); }
  } g{render_guard};

  const auto now_ns = steady_now_ns();
  const auto until_ns = suppress_vsync_until_ns.load(std::memory_order_relaxed);
  host.set_vsync(now_ns >= until_ns);

  // 1. Get the latest frame buffer and current parameters
  const bool force_mono = gui.get_settings_c().force_mono_dmg;
  const bool cgb_mode = is_cgb.load(std::memory_order_relaxed);
  if (force_mono != last_force_mono_dmg || cgb_mode != last_cgb_mode) {
    last_force_mono_dmg = force_mono;
    last_cgb_mode = cgb_mode;
    video_dirty.store(true, std::memory_order_relaxed);
  }

  // 2. Update texture if needed
  if (video_dirty.exchange(false, std::memory_order_acq_rel)) {
    const std::uint32_t *raw_pixels = get_front_buffer();
    host.update_texture(raw_pixels, 160, 144, is_cgb, force_mono);
  }

  // 3. FPS Calculation
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now - last_fps_check)
                              .count();
  // Update FPS readout every 500ms
  if (elapsed_ms >= 500) {
    const uint64_t current_count =
        emulated_frame_count.load(std::memory_order_relaxed);
    const uint64_t frames = current_count - last_frame_count;
    ui_state.current_fps =
        static_cast<double>(frames) * 1000 / static_cast<double>(elapsed_ms);

    last_frame_count = current_count;
    last_fps_check = now;
  }

  // --- PHASE 2: UI COMPOSITION ---
  // 1. Start the ImGui frame
  GbcImGui::new_frame();
  // 2. Build the UI Windows
  bool request_quit = false;
  bool ff_local = false;
  float menu_bar_height = ImGui::GetFrameHeight();
  float status_bar_height = ImGui::GetFrameHeight();
  {
    std::lock_guard lock(ui_mutex);
    sync_io_status_to_ui();
    gui.render(ui_state, host);
    build_savestate_manager_window_locked();
    poll_zip_choice_response();
    request_quit = ui_state.request_quit;
    if (request_quit)
      ui_state.request_quit = false;
    ff_local = ui_state.fast_forward;
    menu_bar_height = ui_state.menu_bar_height;
    status_bar_height = ui_state.status_bar_height;
  }
  fast_forward.store(ff_local, std::memory_order_relaxed);
  if (request_quit)
    running = false;

  if (!startup_window_size_adjusted && menu_bar_height > 0.0f &&
      status_bar_height > 0.0f) {
    if (SDL_Window *window = host.get_window()) {
      const Uint32 flags = SDL_GetWindowFlags(window);
      if (!(flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN))) {
        constexpr int target_w = framebuf_width * scale;
        const int target_h =
            framebuf_height * scale +
            static_cast<int>(std::lround(menu_bar_height + status_bar_height));
        int cur_w = 0;
        int cur_h = 0;
        SDL_GetWindowSize(window, &cur_w, &cur_h);
        if (cur_w != target_w || cur_h != target_h) {
          SDL_SetWindowSize(window, target_w, target_h);
        }
      }
    }
    startup_window_size_adjusted = true;
  }

  // 3. Build debugger windows (if active)
  if (ui_state.show_main_debug_viewer || ui_state.show_breakpoints ||
      ui_state.show_ppu_viewer || ui_state.show_memory_viewer)
    debugger.render(ui_state, gbc);

  // 4. Finalize ImGui frame
  GbcImGui::end_frame();

  // --- PHASE 3: DRAW TO SCREEN ---
  // 1. Clear background
  host.clear_screen();
  // 2. Draw the Emulator Output
  host.draw_texture(menu_bar_height, status_bar_height);
  // 3. Draw the ImGui Overlay
  host.draw_overlay(ImGui::GetDrawData());
  // 4. Swap buffers
  host.present();
}

void SDL3Frontend::emulation_thread_fn(
    const std::stop_token &st, const cart &c,
    const std::optional<std::string> &bios,
    const std::optional<std::filesystem::path> &initial_save_path) {
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
  if (auto *bus = gbc->get_bus(); bus && initial_save_path.has_value()) {
    if (auto *cart_ptr = bus->get_cartridge(); cart_ptr) {
      std::error_code ec;
      if (cart_ptr->has_battery() &&
          std::filesystem::exists(*initial_save_path, ec) &&
          !cart_ptr->load_save_file(*initial_save_path)) {
        Logger::push(LogLevel::Warning, "Save", "Failed to load save file",
                     "Could not load save data from: " +
                         initial_save_path->string());
      }
    }
  }
  std::vector<byte_t> last_saved_snapshot;
  if (auto *bus = gbc->get_bus(); bus) {
    if (auto *cart_ptr = bus->get_cartridge();
        cart_ptr && cart_ptr->has_battery()) {
      const auto ram_view = cart_ptr->ram();
      last_saved_snapshot.assign(ram_view.begin(), ram_view.end());
    }
  }

  auto callback = [this, st]() -> Debug::BreakReason {
    return debugger.on_breakpoint(st, gbc);
  };
  gbc->configure_debugger(Debug::Debugger(callback));

  /* Establish connection with button state */
  auto *joypad = dynamic_cast<Joypad::JOYP *>(
      gbc->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  if (!joypad)
    throw std::logic_error("Failed to configure joypad input");
  auto next_save_poll = Clock::now();
  quicksave_requested.store(false, std::memory_order_relaxed);
  quickload_requested.store(false, std::memory_order_relaxed);
  const auto read_blob = [](const std::filesystem::path &path)
      -> std::optional<std::vector<byte_t>> {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
      return std::nullopt;
    const auto size = f.tellg();
    if (size <= 0)
      return std::nullopt;
    std::vector<byte_t> buf(size);
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char *>(buf.data()), size))
      return std::nullopt;
    return buf;
  };

  /* Run emulation in real-time */
  while (!st.stop_requested()) [[likely]] {
    // Audio sync logic
    // Check how much audio is currently buffered
    const int queued_bytes = host.get_queued_audio_bytes();

    // If we are ahead of the target (and not fast-forwarding), sleep briefly.
    // 1ms should be short enough to prevent underruns
    const int bytes_per_frame =
        host.get_audio_spec().channels * static_cast<int>(sizeof(float));
    const int bytes_per_sec = host.get_audio_spec().freq * bytes_per_frame;
    const int queued_ms =
        bytes_per_sec > 0 ? queued_bytes * 1000 / bytes_per_sec : 0;
    if (!ff && queued_ms > target_queue_ms) {
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

    if (gbc && gbc->savestate_ready()) {
      if (quicksave_requested.exchange(false, std::memory_order_acq_rel)) {
        try {
          const auto blob = gbc->serialize_savestate();
          if (const auto path = write_savestate_bundle(blob, true);
              !path.has_value()) {
            Logger::push(LogLevel::Warning, "Savestate", "Failed to create",
                         "Could not create a quicksave. Check that the "
                         "savestate directory is accessible.");
          } else {
            Logger::push(LogLevel::Status, "Savestate", "Savestate created",
                         path->string());
          }
        } catch (const std::exception &e) {
          Logger::push(LogLevel::Warning, "Savestate", "Failed to create",
                       e.what());
        }
      }

      if (const auto manual_label = consume_manual_savestate_request();
          manual_label.has_value()) {
        try {
          const auto blob = gbc->serialize_savestate();
          if (const auto path =
                  write_savestate_bundle(blob, false, *manual_label);
              !path.has_value()) {
            Logger::push(LogLevel::Warning, "Savestate", "Failed to create",
                         "Could not create a savestate. Check that the "
                         "savestate directory is accessible.");
          }
        } catch (const std::exception &e) {
          Logger::push(LogLevel::Warning, "Savestate", "Failed to create",
                       e.what());
        }
      }

      if (quickload_requested.exchange(false, std::memory_order_acq_rel)) {
        try {
          if (const auto latest_path = latest_savestate_path();
              !latest_path.has_value()) {
            Logger::push(LogLevel::Status, "Savestate",
                         "No savestate available",
                         "No savestate is available to load.");
          } else if (const auto blob = read_blob(*latest_path);
                     !blob.has_value()) {
            Logger::push(
                LogLevel::Warning, "Savestate", "Inaccessible",
                "Could not read savestate from: " + latest_path->string() +
                    ". It may have been moved or deleted.");
          } else {
            gbc->deserialize_savestate(*blob);
            host.clear_audio_stream();
            video_dirty.store(true, std::memory_order_release);
            Logger::push(LogLevel::Status, "Savestate", "Savestate loaded",
                         latest_path->string());
          }
        } catch (const std::exception &e) {
          Logger::push(LogLevel::Warning, "Savestate", "Load failed", e.what());
        }
      }

      if (const auto load_path = consume_savestate_load_request();
          load_path.has_value()) {
        try {
          if (const auto blob = read_blob(*load_path); !blob.has_value()) {
            Logger::push(LogLevel::Warning, "Savestate", "File not found",
                         "Could not read savestate from: " +
                             load_path->string());
          } else {
            gbc->deserialize_savestate(*blob);
            host.clear_audio_stream();
            video_dirty.store(true, std::memory_order_release);
            Logger::push(LogLevel::Status, "Savestate", "Savestate loaded",
                         load_path->string());
          }
        } catch (const std::exception &e) {
          Logger::push(LogLevel::Warning, "Savestate", "Load failed", e.what());
        }
      }
    }

    if (const auto now = Clock::now(); now >= next_save_poll) {
      next_save_poll = now + std::chrono::milliseconds(250);
      if (auto *bus = gbc->get_bus(); bus) {
        if (auto *cart_ptr = bus->get_cartridge();
            cart_ptr && cart_ptr->consume_save_event()) {
          const auto ram_view = cart_ptr->ram();
          if (!ram_view.empty() &&
              (ram_view.size() != last_saved_snapshot.size() ||
               !std::equal(ram_view.begin(), ram_view.end(),
                           last_saved_snapshot.begin()))) {
            last_saved_snapshot.assign(ram_view.begin(), ram_view.end());
            enqueue_save_snapshot(
                std::vector(ram_view.begin(), ram_view.end()));
          }
        }
      }
    }
  }

  if (auto *bus = gbc->get_bus(); bus) {
    if (auto *cart_ptr = bus->get_cartridge();
        cart_ptr && cart_ptr->consume_save_event()) {
      const auto ram_view = cart_ptr->ram();
      if (!ram_view.empty() && (ram_view.size() != last_saved_snapshot.size() ||
                                !std::equal(ram_view.begin(), ram_view.end(),
                                            last_saved_snapshot.begin()))) {
        enqueue_save_snapshot(std::vector(ram_view.begin(), ram_view.end()));
      }
    }
  }
}

void SDL3Frontend::join_emu_thread_if_running() {
  if (emulation_thread.joinable()) {
    emulation_thread.request_stop();
    debugger.request_stop();
    emulation_thread.join();
  }
}

void SDL3Frontend::handle_controller_press(SDL_GamepadButton btn,
                                           bool pressed) {
  byte_t mask{0};

  // We do not enable UI control via controller, so only emulator input
  switch (btn) {
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    mask |= (byte_t)Joypad::JoypadButton::UP;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    mask |= (byte_t)Joypad::JoypadButton::DOWN;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    mask |= (byte_t)Joypad::JoypadButton::LEFT;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    mask |= (byte_t)Joypad::JoypadButton::RIGHT;
    break;

  // Like raylib frontend, give options for A and B buttons
  case SDL_GAMEPAD_BUTTON_SOUTH:
  case SDL_GAMEPAD_BUTTON_WEST:
    mask |= (byte_t)Joypad::JoypadButton::B;
    break;
  case SDL_GAMEPAD_BUTTON_NORTH:
  case SDL_GAMEPAD_BUTTON_EAST:
    mask |= (byte_t)Joypad::JoypadButton::A;
    break;

  case SDL_GAMEPAD_BUTTON_START:
    mask |= (byte_t)Joypad::JoypadButton::START;
    break;
  case SDL_GAMEPAD_BUTTON_BACK:
    mask |= (byte_t)Joypad::JoypadButton::SELECT;
    break;
  default:
    break;
  }

  byte_t current = input_state.buttons.load(std::memory_order_relaxed);
  if (pressed)
    current |= mask;
  else
    current &= static_cast<byte_t>(~mask);
  input_state.buttons.store(current, std::memory_order_relaxed);
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
  if (pressed && key == binds[GK_FF_TOGGLE])
    ui_state.fast_forward = !ui_state.fast_forward;
  // FF Hold (overrides toggle)
  if (key == binds[GK_FF_HOLD])
    ui_state.fast_forward = pressed;
  // Volume up
  if (pressed && key == binds[GK_VOL_UP]) {
    settings.volume = std::min(1.5f, settings.volume + 0.05f);
    host.set_volume(settings.volume);
  }
  // Volume Down
  if (pressed && key == binds[GK_VOL_DOWN]) {
    settings.volume = std::max(0.0f, settings.volume - 0.05f);
    host.set_volume(settings.volume);
  }
  // Monochrome
  if (pressed && key == binds[GK_MONOCHROME])
    settings.force_mono_dmg = !settings.force_mono_dmg;
  // Savestate shortcuts
  if (pressed && key == binds[GK_QUICKSAVE])
    quicksave_requested.store(true, std::memory_order_release);
  if (pressed && key == binds[GK_QUICKLOAD])
    quickload_requested.store(true, std::memory_order_release);
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

bool SDLCALL SDL3Frontend::event_watcher(void *userdata,
                                         const SDL_Event *event) {
  if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
      event->type == SDL_EVENT_WINDOW_MOVED ||
      event->type == SDL_EVENT_WINDOW_EXPOSED) {

    static auto last_draw = std::chrono::steady_clock::now();
    const int min_interval_ms =
        (event->type == SDL_EVENT_WINDOW_MOVED) ? 33 : 16;

    // Force a frame update immediately
    // When the main loop is blocked during windows resizing
    if (const auto now = std::chrono::steady_clock::now();
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw)
            .count() >= min_interval_ms) {
      auto *self = static_cast<SDL3Frontend *>(userdata);

      const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              now.time_since_epoch())
                              .count();
      // Never block in the watcher (avoids deadlocks on DPI-crossing cascades)
      // If UI is currently being updated, skip this forced draw
      if (!self->ui_mutex.try_lock()) {
        last_draw = now; // still rate-limit; don't spin
        return true;
      }
      self->ui_mutex.unlock();

      self->render_frame();
      self->last_forced_redraw_ns.store(now_ns, std::memory_order_relaxed);
      last_draw = now;
    }
  }

  // Return true to allow the event to propagate to SDL_PollEvent queue
  return true;
}
