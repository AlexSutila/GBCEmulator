#include "frontend/sdl3/frontend.hpp"
#include "debugger/print.hpp"
#include <exception>
#include <functional>
#include <picosha2.h>
#include <span>

namespace {
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

std::int64_t SDL3Frontend::steady_now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

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
  clear(black);

  std::error_code ec;
  tmp_root = std::filesystem::temp_directory_path(ec) / "tmp";
  std::filesystem::create_directories(tmp_root, ec);
  debugger.update_state_from_core(gbc);

  // The emulation thread has not produced a frame yet, so seed the texture once
  // to avoid presenting uninitialized pixels on startup.
  host.update_texture(get_front_buffer(), framebuf_width, framebuf_height, false,
                      false);
}

SDL3Frontend::~SDL3Frontend() {
  SDL_RemoveEventWatch(reinterpret_cast<SDL_EventFilter>(event_watcher), this);
  release_savestate_textures_locked();
  gui.shutdown();
}

std::array<std::uint32_t, 160 * 144> SDL3Frontend::get_frame() {
  const int idx = front_index.load(std::memory_order_relaxed);
  std::array<std::uint32_t, 160 * 144> arr{};
  for (auto i = 0; i < framebuf_width * framebuf_height; ++i) {
    arr[i] = framebuffers[idx][i];
  }
  return arr;
}

void SDL3Frontend::put_pixel(const int x, const int y, const std::uint32_t c) {
  if (x < 0 || x >= framebuf_width || y < 0 || y >= framebuf_height) {
    return;
  }

  const int back_index = 1 - front_index.load(std::memory_order_relaxed);
  framebuffers[back_index][y * framebuf_width + x] = c;

  // The PPU writes pixels in raster order, so the final pixel is the natural
  // hand-off point for the completed back buffer.
  if (x + 1 == framebuf_width && y + 1 == framebuf_height) {
    front_index.store(back_index, std::memory_order_release);
    emulated_frame_count.fetch_add(1, std::memory_order_relaxed);
    video_dirty.store(true, std::memory_order_release);
  }
}

void SDL3Frontend::clear(const std::uint32_t c) {
  for (int i = 0; i < framebuf_width * framebuf_height; ++i) {
    for (auto &buffer : framebuffers) {
      buffer[i] = c;
    }
  }
  video_dirty.store(true, std::memory_order_relaxed);
}

void SDL3Frontend::queue_audio_samples(const float *samples, const size_t count) {
  host.queue_audio(samples, count);
}

void SDL3Frontend::start() {
  const auto start_emulation =
      [this](const cart &cart_ctx, const std::string &display_label,
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

  std::string rom_source;
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
    std::string display_label;
    if (consume_rom_io_result(rom_on_disk, display_label)) {
      try {
        cart cart_ctx = load_cart_fs(rom_on_disk.c_str());
        const std::string rom_hash = sha256_hex(cart_ctx.rom_span());
        setup_save_context(cart_ctx, display_label, rom_hash);
        setup_cheat_context(cart_ctx, display_label, rom_hash);
        start_emulation(cart_ctx, display_label, rom_hash, bios_path,
                        active_save_path);
      } catch (const std::exception &e) {
        Logger::push(LogLevel::Warning, "ROM", "Failed to load ROM", e.what());
      }
    }

    consume_load_bios_request(bios_path);
    process_events();
    process_pending_save();

    const auto now_ns = steady_now_ns();
    const auto last_forced =
        last_forced_redraw_ns.load(std::memory_order_relaxed);
    if (now_ns - last_forced > 2'000'000) {
      render_frame();
    }
  }

  emulation_thread.request_stop();
  join_emu_thread_if_running();
  process_pending_save();
}

bool SDL3Frontend::consume_load_rom_request(std::string &rom_path) {
  std::lock_guard lock(ui_mutex);
  if (!ui_state.request_load_rom) {
    return false;
  }

  ui_state.request_load_rom = false;
  rom_path = ui_state.load_rom_path;
  return true;
}

bool SDL3Frontend::consume_load_bios_request(
    std::optional<std::string> &bios_path) {
  std::lock_guard lock(ui_mutex);
  if (!ui_state.request_load_bios && !ui_state.request_unload_bios) {
    return false;
  }

  if (ui_state.request_unload_bios) {
    ui_state.request_unload_bios = false;
    ui_state.request_load_bios = false;
    ui_state.load_bios_path.clear();
    bios_path = std::nullopt;
    gui.clear_bios_path();
    Logger::push(LogLevel::Status, "BIOS", "BIOS unloaded", "");
    return true;
  }

  ui_state.request_load_bios = false;
  bios_path = ui_state.load_bios_path;
  gui.update_bios_path(ui_state.load_bios_path);
  Logger::push(LogLevel::Status, "BIOS", "BIOS selected",
               ui_state.load_bios_path);
  return true;
}
