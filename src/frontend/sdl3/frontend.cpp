#include "frontend/sdl3/frontend.hpp"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_gamepad.h"
#include "debugger/print.hpp"
#include "gbc.hpp"
#include "memory/mmio/mmio.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <picosha2.h>
#include <span>
#include <stdexcept>

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
  debugger.init();
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
      [this](const cart& cart_ctx, const std::string &display_label,
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
      reset_cheat_context();
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
        setup_cheat_context(cart_ctx, display_label, rom_hash);
        start_emulation(cart_ctx, display_label, rom_hash, bios_path,
                        active_save_path);
      } catch (std::exception &e) {
        Logger::push(LogLevel::Warning, "ROM", "Failed to load ROM", e.what());
      }
    }

    /* Handle BIOS selection/unload, takes effect on next ROM insert */
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
  {
    std::lock_guard lock(ui_mutex);
    gui.prepare_dialog_windows(ui_state);
  }
  gui.use_main_context();
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
    if (ui_state.cheats_dirty) {
      ui_state.cheats_dirty = false;
      cheats_revision_.fetch_add(1, std::memory_order_release);
    }
    if (ui_state.cheats_file_dirty) {
      ui_state.cheats_file_dirty = false;
      save_active_cheats_locked();
    }

    if (ui_state.show_savestate_manager &&
        (!GbcImGui::dialog_is_detached(GbcImGui::DialogId::Savestates) ||
         !gui.has_detached_dialog_context(GbcImGui::DialogId::Savestates))) {
      build_savestate_manager_window_locked(false);
    }

    if (ui_state.show_main_debug_viewer &&
        (!GbcImGui::dialog_is_detached(GbcImGui::DialogId::DebugMain) ||
         !gui.has_detached_dialog_context(GbcImGui::DialogId::DebugMain))) {
      debugger.render_dialog(GbcImGui::DialogId::DebugMain, ui_state, gbc,
                             host.get_renderer(), false);
    }
    if (ui_state.show_breakpoints &&
        (!GbcImGui::dialog_is_detached(GbcImGui::DialogId::Breakpoints) ||
         !gui.has_detached_dialog_context(GbcImGui::DialogId::Breakpoints))) {
      debugger.render_dialog(GbcImGui::DialogId::Breakpoints, ui_state, gbc,
                             host.get_renderer(), false);
    }
    if (ui_state.show_memory_viewer &&
        (!GbcImGui::dialog_is_detached(GbcImGui::DialogId::MemoryViewer) ||
         !gui.has_detached_dialog_context(GbcImGui::DialogId::MemoryViewer))) {
      debugger.render_dialog(GbcImGui::DialogId::MemoryViewer, ui_state, gbc,
                             host.get_renderer(), false);
    }
    if (ui_state.show_ppu_viewer &&
        (!GbcImGui::dialog_is_detached(GbcImGui::DialogId::PpuViewer) ||
         !gui.has_detached_dialog_context(GbcImGui::DialogId::PpuViewer))) {
      debugger.render_dialog(GbcImGui::DialogId::PpuViewer, ui_state, gbc,
                             host.get_renderer(), false);
    }

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

  // 3. Finalize ImGui frame
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

  const auto render_detached_dialog = [this]<typename RenderFn>(
                                          const GbcImGui::DialogId id,
                                          RenderFn &&render_fn) {
    std::lock_guard lock(ui_mutex);
    if (!gui.use_detached_dialog_context(id, ui_state))
      return;

    GbcImGui::new_frame();
    render_fn();
    GbcImGui::end_frame();
    gui.present_detached_dialog(id);
  };

  render_detached_dialog(GbcImGui::DialogId::Settings,
                         [&] { gui.render_dialog(GbcImGui::DialogId::Settings,
                                                 ui_state, host); });
  render_detached_dialog(GbcImGui::DialogId::Cheats,
                         [&] { gui.render_dialog(GbcImGui::DialogId::Cheats,
                                                 ui_state, host); });
  render_detached_dialog(GbcImGui::DialogId::Keybinds,
                         [&] { gui.render_dialog(GbcImGui::DialogId::Keybinds,
                                                 ui_state, host); });
  render_detached_dialog(GbcImGui::DialogId::Savestates,
                         [&] { build_savestate_manager_window_locked(true); });
  render_detached_dialog(
      GbcImGui::DialogId::DebugMain,
      [&] { debugger.render_dialog(GbcImGui::DialogId::DebugMain, ui_state, gbc,
                                   gui.active_renderer(), true); });
  render_detached_dialog(
      GbcImGui::DialogId::Breakpoints,
      [&] { debugger.render_dialog(GbcImGui::DialogId::Breakpoints, ui_state,
                                   gbc, gui.active_renderer(), true); });
  render_detached_dialog(
      GbcImGui::DialogId::MemoryViewer,
      [&] { debugger.render_dialog(GbcImGui::DialogId::MemoryViewer, ui_state,
                                   gbc, gui.active_renderer(), true); });
  render_detached_dialog(
      GbcImGui::DialogId::PpuViewer,
      [&] { debugger.render_dialog(GbcImGui::DialogId::PpuViewer, ui_state,
                                   gbc, gui.active_renderer(), true); });

  gui.use_main_context();
}

std::tuple<AddressBus *const, Cartridge *const, Joypad::JOYP *const>
SDL3Frontend::build_emulator_instance(
    const cart &cart, const std::optional<std::string> &bios,
    const std::optional<std::filesystem::path> &initial_save_path) {

  /* Allocate emulator core, which constructor is used depends on existence of
   * an optional BIOS file. */
  if (bios.has_value()) {
    try {
      auto bios_rom = BootROM(bios.value());
      gbc = std::make_unique<GameBoyColor>(*this, bios_rom);
    }

    // Failure to load should resort to no BIOS as fallback
    catch (std::runtime_error &e) {
      Logger::push(LogLevel::Warning, "BIOS", "Failed to load BIOS", e.what());
      gui.clear_bios_path();
      gbc = std::make_unique<GameBoyColor>(*this);
    }
  }
  gbc->insert_cartridge(cart);

  /* Attempt generic resource acquisition of stuff needed later on in the
   * emulation loop. It is better to do all the error handling early on and
   * promise availability downstream. */
  auto *const bus_ptr = gbc->get_bus();
  if (!bus_ptr)
    throw std::runtime_error("Failed to acquire AddressBus resource");
  auto *const cart_ptr = bus_ptr->get_cartridge();
  auto *joyp_ptr = dynamic_cast<Joypad::JOYP *>(
      gbc->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  if (!joyp_ptr)
    throw std::runtime_error("Failed to acquire Cartridge resource");
  return {bus_ptr, cart_ptr, joyp_ptr};
}

/* Not to be confused with FULL-SYSTEM save states, this is specifically
 * tailored to restoring the contents of random access memory saved on
 * cartridges leveraging a battery. */
std::vector<byte_t> SDL3Frontend::prime_sram_saves(
    const std::optional<std::filesystem::path> &initial_save_path,
    Cartridge *const cart_ptr) {
  using namespace std::filesystem;
  std::vector<byte_t> save_snapshot{};

  // Prime existing SRAM save, if it exists
  if (cart_ptr->has_battery() && initial_save_path.has_value()) {
    const auto &save_path = initial_save_path.value();
    if (!exists(save_path) || !cart_ptr->load_save_file(save_path))
      Logger::push(LogLevel::Warning, "Save", "Failed to load save file",
                   "Could not load save data from: " + save_path.string());
  }

  if (cart_ptr->has_battery()) {
    const auto ram_view = cart_ptr->ram();
    save_snapshot.assign(ram_view.begin(), ram_view.end());
  }
  return save_snapshot;
}

void SDL3Frontend::process_sram_save_events(std::vector<byte_t> save_snapshot,
                                            Cartridge *const cart_ptr) {
  if (!cart_ptr->has_battery() || !cart_ptr->consume_sram_save())
    return;
  const auto ram_view = cart_ptr->ram();

  if (!ram_view.empty()) [[unlikely]] {
    const bool altered =
        ram_view.size() != save_snapshot.size() ||
        !std::equal(ram_view.begin(), ram_view.end(), save_snapshot.begin());

    if (altered) [[unlikely]] {
      save_snapshot.assign(ram_view.begin(), ram_view.end());
      enqueue_save_snapshot(std::vector(ram_view.begin(), ram_view.end()));
    }
  }
}

void SDL3Frontend::process_save_state_events() {
  constexpr auto acq = std::memory_order_acq_rel;
  const bool quick_save = quicksave_requested.exchange(false, acq);
  const bool quick_load = quickload_requested.exchange(false, acq);
  const bool manual = manual_preempt_emu_loop.exchange(false, acq);

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

  if (quick_save) [[unlikely]] {
    try {
      const auto blob = gbc->savestate_serialize();
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

  else if (quick_load) [[unlikely]] {
    try {
      if (const auto latest_path = latest_savestate_path();
          !latest_path.has_value()) {
        Logger::push(LogLevel::Status, "Savestate", "No savestate available",
                     "No savestate is available to load.");
      } else if (const auto blob = read_blob(*latest_path); !blob.has_value()) {
        Logger::push(LogLevel::Warning, "Savestate", "Inaccessible",
                     "Could not read savestate from: " + latest_path->string() +
                         ". It may have been moved or deleted.");
      } else {
        gbc->savestate_deserialize(*blob);
        host.clear_audio_stream();
        video_dirty.store(true, std::memory_order_release);
        Logger::push(LogLevel::Status, "Savestate", "Savestate loaded",
                     latest_path->string());
      }
    } catch (const std::exception &e) {
      Logger::push(LogLevel::Warning, "Savestate", "Load failed", e.what());
    }
  }

  /* Manual is set when manual events happen (rather than quick). In this case,
   * since some manual event has come through, we consume any manual saves or
   * load requests and assume one of them will be handled. */
  else if (manual) [[unlikely]] {
    const auto manual_save_label = consume_manual_savestate_request();
    const auto manual_load_path = consume_savestate_load_request();

    // Process manual save event
    if (manual_save_label.has_value()) {
      const auto &lab = manual_save_label.value();
      try {
        const auto blob = gbc->savestate_serialize();
        if (const auto path = write_savestate_bundle(blob, false, lab);
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

    // Process manual load event
    else if (manual_load_path.has_value()) {
      const auto &path = manual_load_path.value();
      try {
        if (const auto blob = read_blob(path); !blob.has_value()) {
          Logger::push(LogLevel::Warning, "Savestate", "File not found",
                       "Could not read savestate from: " + path.string());
        } else {
          gbc->savestate_deserialize(*blob);
          host.clear_audio_stream();
          video_dirty.store(true, std::memory_order_release);
          Logger::push(LogLevel::Status, "Savestate", "Savestate loaded",
                       path.string());
        }
      } catch (const std::exception &e) {
        Logger::push(LogLevel::Warning, "Savestate", "Load failed", e.what());
      }
    }
  }
}

std::vector<GameBoyColor::CheatCode> SDL3Frontend::snapshot_cheats_locked() const {
  std::vector<GameBoyColor::CheatCode> out;
  const auto &src = gui.get_settings_c().cheats;
  out.reserve(src.size());
  for (const auto &entry : src) {
    out.push_back(GameBoyColor::CheatCode{
        .enabled = entry.enabled,
        .code = entry.code,
        .format = entry.format,
    });
  }
  return out;
}

void SDL3Frontend::sync_cheats_to_core(std::uint64_t &last_revision) const {
  if (!gbc)
    return;

  const auto revision = cheats_revision_.load(std::memory_order_acquire);
  if (revision == last_revision)
    return;

  std::vector<GameBoyColor::CheatCode> snapshot;
  {
    std::lock_guard lock(ui_mutex);
    snapshot = snapshot_cheats_locked();
  }
  gbc->configure_cheats(snapshot);
  last_revision = revision;
}

void SDL3Frontend::emulation_thread_fn(
    const std::stop_token &st, const cart &cart,
    const std::optional<std::string> &bios,
    const std::optional<std::filesystem::path> &initial_save_path) {
  auto next_save_poll = Clock::now();
  std::uint64_t cheat_revision_seen = 0;
  bool ff = false;
  clear(black);

  /* Reset visual and auditory components */
  front_index.store(0, std::memory_order_relaxed);
  host.clear_audio_stream();

  /* Prime save state control mechanism */
  quicksave_requested.store(false, std::memory_order_relaxed);
  quickload_requested.store(false, std::memory_order_relaxed);

  /* Re-instantiate emulator instance. We create the callback in this scope so
   * that we can pass along the stop token by reference easily. */
  const auto [bus_ptr, cart_ptr, joyp_ptr] =
      build_emulator_instance(cart, bios, initial_save_path);
  gbc->configure_debugger(Debug::Debugger([this, st]() -> Debug::BreakReason {
    return debugger.on_breakpoint(st, gbc);
  }));
  sync_cheats_to_core(cheat_revision_seen);

  /* Restore previous SRAM content (i.e., emulate battery backed save data) */
  std::vector<byte_t> last_saved_snapshot =
      prime_sram_saves(initial_save_path, cart_ptr);

  /* Run emulation in real-time */
  while (!st.stop_requested()) [[likely]] {
    sync_cheats_to_core(cheat_revision_seen);

    // Audio sync logic, check how much audio is currently buffered
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
    joyp_ptr->set_state(input_state.buttons.load(std::memory_order_relaxed));
    advance_emulator_core(max_catchup_cycles);

    /* Update additional meta-data, avoid mutex acquisition */
    is_cgb.store(gbc->get_sys().cgb_mode);
    ff = fast_forward.load();
    debugger.forward_stop(gbc);

    // Periodic SRAM save backups in case of unexpected quit / crash
    if (const auto now = Clock::now(); now >= next_save_poll) {
      constexpr auto polling_period = std::chrono::milliseconds(250);
      process_sram_save_events(last_saved_snapshot, cart_ptr);
      next_save_poll = now + polling_period;
    }
  }

  // Handle dangling SRAM save backups before quitting
  process_sram_save_events(last_saved_snapshot, cart_ptr);
}

void SDL3Frontend::advance_emulator_core(const int cycles) {
  const bool preempt = should_preempt_emu_loop(); // Heads up, this is latent

  /* If a save event came in prior to this advancement, we want to preempt the
   * main emulation loop and handle it as soon as the system is in a safe state
   * to do so. */
  for (auto i{0}; i < cycles; i++) {
    gbc->step();

    // Note: Keep short circuit eval on `gbc->savestate_ready()` if possible
    if (preempt && gbc->savestate_ready()) [[unlikely]] {
      process_save_state_events();
      return; // Drop the rest of the cycles for this synchronization perdiod,
              // if we hear an audio pop it's not the end of the world.
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
  if (!ui_state.request_load_bios && !ui_state.request_unload_bios)
    return false;

  if (ui_state.request_unload_bios) {
    ui_state.request_unload_bios = false;
    ui_state.request_load_bios = false;
    ui_state.load_bios_path.clear();
    bios_path = std::nullopt;
    gui.clear_bios_path();
    Logger::push(LogLevel::Status, "BIOS", "BIOS unloaded", "");
    return true;
  }

  /* Denote new BIOS path */
  ui_state.request_load_bios = false;
  bios_path = ui_state.load_bios_path;
  gui.update_bios_path(ui_state.load_bios_path);
  Logger::push(LogLevel::Status, "BIOS", "BIOS selected",
               ui_state.load_bios_path);
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
