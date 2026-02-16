#include "frontend/sdl3/frontend.hpp"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_gamepad.h"
#include "debugger/print.hpp"
#include <random>
#include <curl/curl.h>
#include <miniz.h>


namespace {
  bool looks_like_url(const std::string &s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
  }

  std::string to_lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  bool has_ext(const std::string &path, const std::string &ext) {
    const auto p = to_lower(path);
    const auto e = to_lower(ext);
    if (p.size() < e.size()) return false;
    return p.compare(p.size() - e.size(), e.size(), e) == 0;
  }

  bool file_starts_with_zip_magic(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    unsigned char sig[4]{};
    f.read(reinterpret_cast<char *>(sig), 4);
    return sig[0] == 0x50 && sig[1] == 0x4B && sig[2] == 0x03 && sig[3] == 0x04;
  }

  std::filesystem::path make_temp_file(const std::filesystem::path &root,
                                              std::string_view suffix) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dis;

    for (int attempt = 0; attempt < 32; ++attempt) {
      const auto name = "gbc_" + std::to_string(dis(gen)) + std::string(suffix);
      auto p = root / name;
      if (std::error_code ec; !std::filesystem::exists(p, ec)) return p;
    }

    const auto name =
        "gbc_" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
        std::string(suffix);
    return root / name;
  }

  struct CurlDownloadCtx {
    std::atomic<float> *progress{};
    std::stop_token st;
  };

  size_t curl_write_file_cb(const char *ptr, const size_t size, const size_t nmemb, void *userdata) {
    auto *fp = static_cast<FILE *>(userdata);
    return std::fwrite(ptr, size, nmemb, fp) * size;
  }

  int curl_xferinfo_cb(void *clientp, const curl_off_t dltotal, const curl_off_t dlnow,
                              curl_off_t, curl_off_t) {
    const auto *ctx = static_cast<CurlDownloadCtx *>(clientp);
    if (ctx && ctx->st.stop_requested()) return 1;
    if (!ctx || !ctx->progress) return 0;

    if (dltotal > 0) {
      const float p = static_cast<float>(dlnow) / static_cast<float>(dltotal);
      ctx->progress->store(std::clamp(p, 0.0f, 1.0f), std::memory_order_relaxed);
    } else {
      ctx->progress->store(-1.0f, std::memory_order_relaxed);
    }
    return 0;
  }

  std::int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               Clock::now().time_since_epoch())
        .count();
  }

  std::string process_path(const std::string &path) {
    if (path.find("file:/", 0) == 0) {
      return path.substr(6);
    }
    return path;
  }
}
  void SDL3Frontend::sync_io_status_to_ui() {
  ui_state.io_busy = io_busy.load(std::memory_order_relaxed);
  ui_state.io_progress = io_progress.load(std::memory_order_relaxed);
  {
    std::lock_guard lk(io_status_mutex);
    ui_state.io_status = io_status;
  }
}

bool SDL3Frontend::consume_rom_io_result(std::string &rom_path_on_disk,
                                        std::string &display_label) {
  std::lock_guard lk(rom_io_mutex);
  if (!rom_ready_path) return false;
  rom_path_on_disk = std::move(*rom_ready_path);
  display_label = std::move(rom_ready_label);
  rom_ready_path.reset();
  rom_ready_label.clear();
  return true;
}

int SDL3Frontend::request_zip_choice_blocking(const std::string &zip_label,
                                              const std::vector<std::string> &entries,
                                              const std::stop_token &st) {
  {
    std::lock_guard lock(ui_mutex);
    ui_state.zip_picker_title = zip_label;
    ui_state.zip_rom_entries = entries;
    ui_state.zip_rom_selected_idx = 0;
    ui_state.zip_picker_action = 0;
    ui_state.show_zip_picker_popup = true;
  }

  std::unique_lock lk(zip_choice_mutex);
  zip_choice_pending = true;
  zip_choice_result = -1;
  zip_choice_cancelled = false;

  zip_choice_cv.wait(lk, [&] { return !zip_choice_pending || st.stop_requested(); });

  if (st.stop_requested() || zip_choice_cancelled) return -1;
  return zip_choice_result;
}

// Must be called with ui_mutex held
void SDL3Frontend::poll_zip_choice_response() {
  int action = 0;
  int idx = 0;
  action = ui_state.zip_picker_action;
  idx = ui_state.zip_rom_selected_idx;
  if (action != 0) {
    ui_state.zip_picker_action = 0;
    ui_state.zip_picker_title.clear();
    ui_state.zip_rom_entries.clear();
  }
  if (action == 0) return;

  {
    std::lock_guard lk(zip_choice_mutex);
    if (!zip_choice_pending) return;
    zip_choice_result = idx;
    zip_choice_cancelled = (action == 2);
    zip_choice_pending = false;
  }
  zip_choice_cv.notify_all();
}

void SDL3Frontend::handle_drop(const SDL_Event &e) {
  if (!e.drop.data) return;
  std::lock_guard lock(ui_mutex);
  ui_state.load_rom_path = process_path(e.drop.data);   // file path or URL text
  ui_state.request_load_rom = true;
}

void SDL3Frontend::start_rom_io_job(const std::string &source) {
  if (rom_io_thread.joinable()) {
    {
      std::lock_guard lk(zip_choice_mutex);
      zip_choice_cancelled = true;
      zip_choice_pending = false;
    }
    zip_choice_cv.notify_all();
    rom_io_thread.request_stop();
    rom_io_thread.join();
  }

  io_busy.store(true, std::memory_order_relaxed);
  io_progress.store(-1.0f, std::memory_order_relaxed);
  {
    std::lock_guard lk(io_status_mutex);
    io_status = "Preparing ROM...";
  }

  rom_io_thread = std::jthread([this, source](const std::stop_token& st) {
    auto set_status = [&](std::string s) {
      std::lock_guard lk(io_status_mutex);
      io_status = std::move(s);
    };

    auto cleanup_temp = [&] {
      std::error_code ec;
      if (last_tmp_rom) std::filesystem::remove(*last_tmp_rom, ec);
      if (last_tmp_zip) std::filesystem::remove(*last_tmp_zip, ec);
      last_tmp_rom.reset();
      last_tmp_zip.reset();
    };

    cleanup_temp();

    auto fail = [&](const std::string &msg) {
      Logger::push(LogLevel::Warning, "ROM", "ROM load failed", msg);
      set_status(msg);
      io_busy.store(false, std::memory_order_relaxed);
    };

    auto succeed = [&](const std::string &rom_on_disk, const std::string &label) {
      {
        std::lock_guard lk(rom_io_mutex);
        rom_ready_path = rom_on_disk;
        rom_ready_label = label;
      }
      io_busy.store(false, std::memory_order_relaxed);
      io_progress.store(-1.0f, std::memory_order_relaxed);
      set_status("Ready");
    };

    std::filesystem::path local_path;
    std::string label = source;

    if (looks_like_url(source)) {
      set_status("Downloading...");
      io_progress.store(0.0f, std::memory_order_relaxed);

      std::string suffix = ".bin";
      {
        const auto q = source.find_first_of("?#");
        const std::string base = (q == std::string::npos) ? source : source.substr(0, q);
        const auto slash = base.find_last_of('/');
        const auto dot = base.find_last_of('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
          suffix = base.substr(dot);
          if (suffix.size() > 16) suffix = ".bin";
        }
      }

      local_path = make_temp_file(tmp_root, suffix);
      FILE *fp = std::fopen(local_path.string().c_str(), "wb");
      if (!fp) {
        fail("Failed to create temp file for download");
        return;
      }

      CURL *curl = curl_easy_init();
      if (!curl) {
        std::fclose(fp);
        fail("curl_easy_init() failed");
        return;
      }

      CurlDownloadCtx ctx;
      ctx.progress = &io_progress;
      ctx.st = st;

      curl_easy_setopt(curl, CURLOPT_URL, source.c_str());
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "gbc_full/1.0");
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_file_cb);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_xferinfo_cb);
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);

      const CURLcode res = curl_easy_perform(curl);
      curl_easy_cleanup(curl);
      std::fclose(fp);

      if (st.stop_requested()) {
        std::error_code ec;
        std::filesystem::remove(local_path, ec);
        set_status("Download cancelled");
        io_busy.store(false, std::memory_order_relaxed);
        return;
      }

      if (res != CURLE_OK) {
        std::error_code ec;
        std::filesystem::remove(local_path, ec);
        fail(std::string("Download failed: ") + curl_easy_strerror(res));
        return;
      }

      last_tmp_zip = local_path; // downloaded file (rom or zip)
    } else {
      local_path = source;
    }

    if (st.stop_requested()) {
      set_status("Cancelled");
      io_busy.store(false, std::memory_order_relaxed);
      return;
    }

    const bool is_zip = has_ext(local_path.string(), ".zip") ||
                        file_starts_with_zip_magic(local_path);

    if (!is_zip) {
      if (looks_like_url(source)) last_tmp_rom = local_path;
      succeed(local_path.string(), label);
      return;
    }

    set_status("Scanning ZIP...");
    io_progress.store(-1.0f, std::memory_order_relaxed);

    std::vector<int> candidate_indices;
    std::vector<std::string> candidate_names;

    {
      mz_zip_archive zip{};
      if (!mz_zip_reader_init_file(&zip, local_path.string().c_str(), 0)) {
        fail("Failed to open ZIP file");
        return;
      }
      const int n = static_cast<int>(mz_zip_reader_get_num_files(&zip));
      for (int i = 0; i < n; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat stt{};
        if (!mz_zip_reader_file_stat(&zip, i, &stt)) continue;
        const std::string name = stt.m_filename[0] != '\0' ? stt.m_filename : "";
        if (has_ext(name, ".gb") || has_ext(name, ".gbc")) {
          candidate_indices.push_back(i);
          candidate_names.push_back(name);
        }
      }
      mz_zip_reader_end(&zip);
    }

    if (candidate_indices.empty()) {
      fail("ZIP did not contain any .gb/.gbc files");
      return;
    }

    int chosen = 0;
    if (candidate_indices.size() > 1) {
      set_status("Choose ROM from ZIP...");
      chosen = request_zip_choice_blocking(
          std::string("ROMs found in ZIP (") +
              std::filesystem::path(local_path).filename().string() + ")",
          candidate_names, st);
      if (chosen < 0 || chosen >= static_cast<int>(candidate_indices.size())) {
        set_status("ZIP selection cancelled");
        io_busy.store(false, std::memory_order_relaxed);
        return;
      }
    }

    const int chosen_idx = candidate_indices[chosen];
    const std::string chosen_name = candidate_names[chosen];

    set_status("Extracting ROM...");
    io_progress.store(-1.0f, std::memory_order_relaxed);

    const std::string out_suffix = has_ext(chosen_name, ".gbc") ? ".gbc" : ".gb";
    const auto out_rom = make_temp_file(tmp_root, out_suffix);

    {
      mz_zip_archive zip{};
      if (!mz_zip_reader_init_file(&zip, local_path.string().c_str(), 0)) {
        fail("Failed to re-open ZIP for extraction");
        return;
      }
      const bool ok =
          mz_zip_reader_extract_to_file(&zip, chosen_idx, out_rom.string().c_str(), 0) != 0;
      mz_zip_reader_end(&zip);
      if (!ok) {
        std::error_code ec;
        std::filesystem::remove(out_rom, ec);
        fail("Failed to extract ROM from ZIP");
        return;
      }
    }

    last_tmp_rom = out_rom;
    label = source + " :: " + chosen_name;
    succeed(out_rom.string(), label);
  });
}

SDL3Frontend::SDL3Frontend() : host(framebuf_width, framebuf_height, scale) {
  host.init_audio();
  gui.init(host);
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

  // We have to update this manually once because the emulation loop has not
  // started yet to do it for us. If we don't do this, we may end up seeing a
  // garbage value being used for the initial screen color.
  const std::uint32_t *raw_pixels = get_front_buffer();
  host.update_texture(raw_pixels, 160, 144, false, false);
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
  std::optional<std::string> bios_path =
      gui.get_settings_c().prev_bios_path.empty()
          ? std::nullopt
          : std::make_optional(gui.get_settings_c().prev_bios_path);
  clear(black);

  std::string rom_source{};
  while (running.load()) {
    if (consume_load_rom_request(rom_source)) {
      join_emu_thread_if_running();
      start_rom_io_job(rom_source);
    }
    std::string rom_on_disk;
    if (std::string display_label; consume_rom_io_result(rom_on_disk, display_label)) {
      try {
        cart cart_ctx = load_cart_fs(rom_on_disk.c_str());
        emulation_thread = std::jthread(
          std::bind_front(&SDL3Frontend::emulation_thread_fn, this),
                                        cart_ctx, bios_path);
        {
          std::lock_guard lock(ui_mutex);
          ui_state.load_rom_path = display_label;
          ui_state.cart_info = Debug::describe_cart(cart_ctx);
        }
        gui.update_rom_path(display_label);
      } catch (std::exception &e) {
        Logger::push(LogLevel::Warning, "ROM", "Failed to load ROM", e.what());
      }
    }

    /* Handle BIOS selection, won't take effect until ROM re-inserted */
    consume_load_bios_request(bios_path);
    process_events();
    // Don't force a redraw if the watcher just forced it
    const auto now_ns = steady_now_ns();
    if (const auto last_forced = last_forced_redraw_ns.load(std::memory_order_relaxed);
      now_ns - last_forced > 2'000'000) // ~2ms
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
    {
      std::lock_guard lock(ui_mutex);
      if (gui.process_event(e, ui_state))
        continue;
    }
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
  }
}

void SDL3Frontend::render_frame() {
  // --- PHASE 1: PREPARE TEXTURE ---
  // 0. Check if we need to update the texture (avoid redundant GPU uploads)
  if (render_guard.test_and_set(std::memory_order_acquire)) return;
  struct Guard { std::atomic_flag &f; ~Guard(){ f.clear(std::memory_order_release); } } g{render_guard};

  const auto now_ns = steady_now_ns();
  const auto until_ns = suppress_vsync_until_ns.load(std::memory_order_relaxed);
  host.set_vsync(now_ns >= until_ns);

  // 1. Get the latest frame buffer and current parameters
  const bool force_mono = gui.get_settings_c().force_mono_dmg;
  const bool cgb_mode   = is_cgb.load(std::memory_order_relaxed);
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
  {
    std::lock_guard lock(ui_mutex);
    sync_io_status_to_ui();
    gui.render(ui_state, host);
    poll_zip_choice_response();
    request_quit = ui_state.request_quit;
    if (request_quit) ui_state.request_quit = false;
    ff_local = ui_state.fast_forward;
  }
  fast_forward.store(ff_local, std::memory_order_relaxed);
  if (request_quit) running = false;

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
    const int bytes_per_frame = host.get_audio_spec().channels * static_cast<int>(sizeof(float));
    const int bytes_per_sec   = host.get_audio_spec().freq * bytes_per_frame;
    const int queued_ms       = bytes_per_sec > 0 ? queued_bytes * 1000 / bytes_per_sec : 0;
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
    const int min_interval_ms = (event->type == SDL_EVENT_WINDOW_MOVED) ? 33 : 16;

    // Force a frame update immediately
    // When the main loop is blocked during windows resizing
    if (const auto now = std::chrono::steady_clock::now();
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw)
            .count() >= min_interval_ms) {
      auto *self = static_cast<SDL3Frontend *>(userdata);

      const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
      // Disable vsync for a short grace window after move/resize events
      self->suppress_vsync_until_ns.store(now_ns + 150'000'000, std::memory_order_relaxed);
      self->render_frame();
      self->last_forced_redraw_ns.store(now_ns, std::memory_order_relaxed);
      last_draw = now;
    }
  }

  // Return true to allow the event to propagate to SDL_PollEvent queue
  return true;
}
