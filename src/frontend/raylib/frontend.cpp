#include "frontend/raylib/frontend.hpp"
#include "gbc.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <raylib.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

EM_JS(int, web_load_active_save, (std::uint8_t *out_ptr, int out_cap), {
  try {
    if (!out_ptr || out_cap <= 0)
      return 0;
    const api = globalThis.IroGBSaves;
    if (!api || typeof api.loadActiveSram != = "function")
      return 0;

    const bytes = api.loadActiveSram();
    if (!(bytes instanceof Uint8Array) || bytes.length == = 0)
      return 0;

    const n = Math.min(bytes.length, out_cap | 0) | 0;
    HEAPU8.set(bytes.subarray(0, n), out_ptr >>> 0);
    return n;
  } catch (err) {
    console.warn("Failed to restore SRAM from localStorage:", err);
    return -1;
  }
});

EM_JS(int, web_save_active_save, (const std::uint8_t *data_ptr, int len), {
  try {
    if (!data_ptr || len <= 0)
      return 0;
    const api = globalThis.IroGBSaves;
    if (!api || typeof api.saveActiveSram != = "function")
      return 0;

    const start = data_ptr >>> 0;
    const end = (start + (len | 0)) >>> 0;
    const bytes = new Uint8Array(HEAPU8.subarray(start, end));
    return api.saveActiveSram(bytes) ? 1 : 0;
  } catch (err) {
    console.warn("Failed to persist SRAM to localStorage:", err);
    return -1;
  }
});

EM_JS(int, web_load_active_state, (std::uint8_t * out_ptr, int out_cap), {
  try {
    const api = globalThis.IroGBSaves;
    if (!api || typeof api.loadActiveState !== "function") return 0;

    const bytes = api.loadActiveState();
    if (!(bytes instanceof Uint8Array) || bytes.length === 0) return 0;

    if (!out_ptr || out_cap <= 0) {
      return bytes.length | 0;
    }

    const n = Math.min(bytes.length, out_cap | 0) | 0;
    HEAPU8.set(bytes.subarray(0, n), out_ptr >>> 0);
    return n;
  } catch (err) {
    console.warn("Failed to restore savestate from localStorage:", err);
    return -1;
  }
});

EM_JS(int, web_save_active_state, (const std::uint8_t * data_ptr, int len), {
  try {
    if (!data_ptr || len <= 0) return 0;
    const api = globalThis.IroGBSaves;
    if (!api || typeof api.saveActiveState !== "function") return 0;

    const start = data_ptr >>> 0;
    const end = (start + (len | 0)) >>> 0;
    const bytes = new Uint8Array(HEAPU8.subarray(start, end));
    return api.saveActiveState(bytes) ? 1 : 0;
  } catch (err) {
    console.warn("Failed to persist savestate to localStorage:", err);
    return -1;
  }
});

EM_JS(int, web_set_pending_state_thumb_rgba,
      (const std::uint8_t * data_ptr, int len, int width, int height), {
        try {
          if (!data_ptr || len <= 0 || width <= 0 || height <= 0)
            return 0;
          const api = globalThis.IroGBSaves;
          if (!api || typeof api.setPendingStateThumbnailRgba !== "function")
            return 0;

          const start = data_ptr >>> 0;
          const end = (start + (len | 0)) >>> 0;
          const bytes = new Uint8Array(HEAPU8.subarray(start, end));
          return api.setPendingStateThumbnailRgba(bytes, width | 0, height | 0)
                     ? 1
                     : 0;
        } catch (err) {
          console.warn("Failed to stage savestate thumbnail:", err);
          return -1;
        }
      });

constexpr double kWebSramFlushDebounceMs = 750.0;

static std::uint8_t g_web_input_state = 0;

extern "C" {
// 0=Right, 1=Left, 2=Up, 3=Down, 4=A, 5=B, 6=Select, 7=Start
EMSCRIPTEN_KEEPALIVE void emscripten_set_button(const int btn,
                                                const int pressed) {
  using JB = Joypad::JoypadButton;
  std::uint8_t mask = 0;
  switch (btn) {
  case 0:
    mask = static_cast<std::uint8_t>(JB::RIGHT);
    break;
  case 1:
    mask = static_cast<std::uint8_t>(JB::LEFT);
    break;
  case 2:
    mask = static_cast<std::uint8_t>(JB::UP);
    break;
  case 3:
    mask = static_cast<std::uint8_t>(JB::DOWN);
    break;
  case 4:
    mask = static_cast<std::uint8_t>(JB::A);
    break;
  case 5:
    mask = static_cast<std::uint8_t>(JB::B);
    break;
  case 6:
    mask = static_cast<std::uint8_t>(JB::SELECT);
    break;
  case 7:
    mask = static_cast<std::uint8_t>(JB::START);
    break;
  default:
    return;
  }
  if (pressed)
    g_web_input_state |= mask;
  else
    g_web_input_state &= static_cast<std::uint8_t>(~mask);
}

EMSCRIPTEN_KEEPALIVE void emscripten_clear_buttons() { g_web_input_state = 0; }
EMSCRIPTEN_KEEPALIVE void emscripten_set_master_volume(const float v) {
  SetMasterVolume(std::clamp(v, 0.0f, 1.0f));
}
} // extern "C"

static void frame_cb(void *user) {
  auto *fe = static_cast<RaylibFrontend *>(user);
  fe->tick_web();
}
#endif // __EMSCRIPTEN__

namespace {
std::string sanitize_label(std::string s, const std::size_t max_len = 32) {
  for (char &c : s) {
    const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
    c = keep ? c : '_';
  }

  while (!s.empty() && s.front() == '_')
    s.erase(s.begin());
  while (!s.empty() && s.back() == '_')
    s.pop_back();

  if (s.size() > max_len)
    s.resize(max_len);

  if (s.empty())
    s = "cartridge";
  return s;
}

std::optional<std::vector<byte_t>>
read_blob_file(const std::filesystem::path &path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    return std::nullopt;

  const auto size = f.tellg();
  if (size <= 0)
    return std::nullopt;

  std::vector<byte_t> out(static_cast<std::size_t>(size));
  f.seekg(0, std::ios::beg);
  if (!f.read(reinterpret_cast<char *>(out.data()), size))
    return std::nullopt;
  return out;
}

bool write_blob_file(const std::filesystem::path &path,
                     const std::vector<byte_t> &data) {
  if (path.empty() || data.empty())
    return false;

  if (const auto parent = path.parent_path(); !parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
  }

  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f)
    return false;

  f.write(reinterpret_cast<const char *>(data.data()),
          static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}
} // namespace

static std::uint32_t format_color(const std::uint32_t c) {
  return ((c & 0x00FF0000) >> 16) | ((c & 0x0000FF00)) |
         ((c & 0x000000FF) << 16) | 0xFF000000;
}

std::filesystem::path RaylibFrontend::build_desktop_savestate_path(
    const cart &c) {
  std::string stem = c.file_path.stem().string();
  if (stem.empty())
    stem = c.header.title();
  stem = sanitize_label(std::move(stem));

  std::ostringstream checksum_stream;
  checksum_stream << std::hex << std::setfill('0') << std::setw(4)
                  << static_cast<unsigned>(c.header.global_checksum);
  const std::string checksum = checksum_stream.str();

  std::filesystem::path root = "./savestates";
  std::error_code ec;
  if (root.is_relative()) {
    if (const auto cwd = std::filesystem::current_path(ec); !ec)
      root = cwd / root;
  }

  return (root / (stem + " - " + checksum + ".state")).lexically_normal();
}

RaylibFrontend::RaylibFrontend(const cart &c) {
  gbc->insert_cartridge(c);
#ifndef __EMSCRIPTEN__
  quick_savestate_path_ = build_desktop_savestate_path(c);
#endif
}

RaylibFrontend::~RaylibFrontend() {
#ifdef __EMSCRIPTEN__
  flush_web_save_now();
#endif
  if (audio_ready)
    ::UnloadAudioStream(audio_stream);
  CloseAudioDevice();
  if (texture.id)
    ::UnloadTexture(texture);
  CloseWindow();
}

std::array<std::uint32_t, 144 * 160> RaylibFrontend::get_frame() {
  return frame_buf.at(display_idx);
}

void RaylibFrontend::put_pixel(const int x, const int y,
                               const std::uint32_t c) {
  if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) [[unlikely]]
    return;
  frame_buf.at(write_idx).at(y * fb_width + x) = format_color(c);

  // Swap as frame becomes ready to avoid screen tears
  if (x == fb_width - 1 && y == fb_height - 1) {
    display_idx = write_idx;
    write_idx = (write_idx + 1) % nbuf;
    frame_ready = true;
  }
}

void RaylibFrontend::clear(const std::uint32_t c) {
  for (auto &buf : frame_buf)
    buf.fill(format_color(c));
  frame_ready = false;
  write_idx = 0;
  display_idx = 0;
}

void RaylibFrontend::read_controller_inputs(std::uint8_t &input_state) {
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::UP);
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::DOWN);
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::LEFT);
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::RIGHT);
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::START);
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::SELECT);

  // Since the right face may have multiple buttons, bind multiple to a single
  // virtual key. Better to have options.
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
      IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::B);
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
      IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::A);
}

void RaylibFrontend::read_keyboard_inputs(std::uint8_t &input_state) {
  if (IsKeyDown(KEY_UP))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::UP);
  if (IsKeyDown(KEY_DOWN))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::DOWN);
  if (IsKeyDown(KEY_LEFT))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::LEFT);
  if (IsKeyDown(KEY_RIGHT))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::RIGHT);
  if (IsKeyDown(KEY_Z))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::A);
  if (IsKeyDown(KEY_X))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::B);
  if (IsKeyDown(KEY_BACKSPACE))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::SELECT);
  if (IsKeyDown(KEY_ENTER))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::START);
}

void RaylibFrontend::read_inputs() {
  std::uint8_t input_state{};

#ifdef __EMSCRIPTEN__
  // On web builds, keyboard input is handled in JS so it can be rebound.
  input_state = g_web_input_state;
#else
  read_keyboard_inputs(input_state);
#endif
#ifndef __EMSCRIPTEN__
  if (IsKeyPressed(KEY_F4))
    request_quicksave();
  if (IsKeyPressed(KEY_F8))
    request_quickload();
#endif

  // Handle controller input, we casually let it overwrite keyboard for
  // the sake of simplicity and the fact that you cant really use both
  // at the same time.
  if (IsGamepadAvailable(0))
    read_controller_inputs(input_state);

  // Transfer button state to internal joypad register
  auto *const joyp = dynamic_cast<Joypad::JOYP *>(
      gbc->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  if (!joyp)
    throw std::runtime_error("RaylibFrontend::read_inputs()");
  joyp->set_state(input_state);
}

void RaylibFrontend::request_quicksave() { quicksave_requested = true; }

void RaylibFrontend::request_quickload() { quickload_requested = true; }

bool RaylibFrontend::has_pending_savestate_request() const {
  return quicksave_requested || quickload_requested;
}

std::vector<std::uint8_t> RaylibFrontend::capture_savestate_thumbnail_rgba() const {
  const auto &src = frame_buf.at(display_idx);
  if (src.empty())
    return {};

  std::vector<std::uint8_t> out(savestate_thumb_w * savestate_thumb_h * 4);
  for (int y = 0; y < savestate_thumb_h; ++y) {
    const int sy = (y * fb_height) / savestate_thumb_h;
    for (int x = 0; x < savestate_thumb_w; ++x) {
      const int sx = (x * fb_width) / savestate_thumb_w;
      const std::uint32_t px =
          src[static_cast<std::size_t>(sy) * fb_width + sx];
      const std::size_t out_i =
          static_cast<std::size_t>(y * savestate_thumb_w + x) * 4;
      out[out_i + 0] = static_cast<std::uint8_t>(px & 0xFF);         // R
      out[out_i + 1] = static_cast<std::uint8_t>((px >> 8) & 0xFF);  // G
      out[out_i + 2] = static_cast<std::uint8_t>((px >> 16) & 0xFF); // B
      out[out_i + 3] = static_cast<std::uint8_t>((px >> 24) & 0xFF); // A
    }
  }
  return out;
}

void RaylibFrontend::process_quicksave_request() {
  try {
    const auto blob = gbc->serialize_savestate();
    if (blob.empty()) {
      TraceLog(LOG_WARNING, "Savestate save failed: empty blob");
      return;
    }

#ifdef __EMSCRIPTEN__
    if (const auto thumb = capture_savestate_thumbnail_rgba(); !thumb.empty()) {
      (void)web_set_pending_state_thumb_rgba(
          thumb.data(), static_cast<int>(thumb.size()), savestate_thumb_w,
          savestate_thumb_h);
    }
    const int rc =
        web_save_active_state(blob.data(), static_cast<int>(blob.size()));
    if (rc > 0) {
      TraceLog(LOG_INFO, "Savestate quicksave created (web localStorage)");
    } else {
      TraceLog(LOG_WARNING,
               "Savestate save failed: localStorage backend unavailable");
    }
#else
    if (quick_savestate_path_.empty()) {
      TraceLog(LOG_WARNING, "Savestate save failed: no destination path");
      return;
    }

    if (write_blob_file(quick_savestate_path_, blob)) {
      TraceLog(LOG_INFO, "Savestate quicksave created: %s",
               quick_savestate_path_.string().c_str());
    } else {
      TraceLog(LOG_WARNING, "Savestate save failed: %s",
               quick_savestate_path_.string().c_str());
    }
#endif
  } catch (const std::exception &e) {
    TraceLog(LOG_WARNING, "Savestate save failed: %s", e.what());
  }
}

void RaylibFrontend::process_quickload_request() {
  try {
    std::vector<byte_t> blob{};

#ifdef __EMSCRIPTEN__
    const int size = web_load_active_state(nullptr, 0);
    if (size <= 0) {
      TraceLog(LOG_INFO, "Savestate quickload skipped: no state available");
      return;
    }

    blob.resize(static_cast<std::size_t>(size));
    const int copied =
        web_load_active_state(blob.data(), static_cast<int>(blob.size()));
    if (copied <= 0 || copied > size) {
      TraceLog(LOG_WARNING, "Savestate quickload failed: invalid payload");
      return;
    }
    blob.resize(static_cast<std::size_t>(copied));
#else
    if (quick_savestate_path_.empty()) {
      TraceLog(LOG_WARNING, "Savestate quickload failed: no source path");
      return;
    }

    const auto file_blob = read_blob_file(quick_savestate_path_);
    if (!file_blob.has_value()) {
      TraceLog(LOG_INFO, "Savestate quickload skipped: %s not found",
               quick_savestate_path_.string().c_str());
      return;
    }
    blob = std::move(*file_blob);
#endif

    gbc->deserialize_savestate(blob);
    rb_head = 0;
    rb_tail = 0;
    rb_size = 0;
    audio_prime = 2;
    TraceLog(LOG_INFO, "Savestate quickload complete");
  } catch (const std::exception &e) {
    TraceLog(LOG_WARNING, "Savestate quickload failed: %s", e.what());
  }
}

void RaylibFrontend::process_pending_savestate_request() {
  if (quicksave_requested) {
    quicksave_requested = false;
    process_quicksave_request();
  } else if (quickload_requested) {
    quickload_requested = false;
    process_quickload_request();
  }
}

void RaylibFrontend::advance_cycles_with_preemption(std::size_t cycles) {
  if (has_pending_savestate_request() && gbc->savestate_ready()) {
    process_pending_savestate_request();
    return;
  }

  while (cycles > 0) {
    gbc->step();
    --cycles;

    if (has_pending_savestate_request() && gbc->savestate_ready()) {
      process_pending_savestate_request();
      return;
    }
  }
}

void RaylibFrontend::step_frame() {
  constexpr std::size_t cycles_per_frame = 70224;
  advance_cycles_with_preemption(cycles_per_frame);
}

void RaylibFrontend::present() {
  if (!frame_ready)
    return;
  frame_ready = false;

  // Rendering
  UpdateTexture(texture, frame_buf.at(display_idx).data());
  BeginDrawing();
  DrawTexturePro(texture,
                 Rectangle{0, 0, static_cast<float>(fb_width),
                           static_cast<float>(fb_height)},
                 Rectangle{0, 0, static_cast<float>(GetScreenWidth()),
                           static_cast<float>(GetScreenHeight())},
                 Vector2{0, 0}, 0.0f, WHITE);
  EndDrawing();
}

// ---- Audio ring buffer helpers (rb_size counts floats) ----
void RaylibFrontend::queue_audio_samples(const float *samples,
                                         std::size_t sample_count) {
  if (!samples || sample_count == 0)
    return;

  constexpr std::size_t cap = ring_samples;

  // Hard cap: never overflow the ring
  if (sample_count >= cap) {
    samples += (sample_count - cap);
    sample_count = cap;
  }
  if (const std::size_t free = cap - rb_size; sample_count > free) {
    const std::size_t drop = sample_count - free;
    rb_tail = (rb_tail + drop) % cap;
    rb_size -= drop;
  }

  // Copy in (handle wrap)
  std::size_t to_write = sample_count;
  while (to_write) {
    const std::size_t chunk = std::min(to_write, cap - rb_head);
    std::memcpy(&audio_rb[rb_head], samples, chunk * sizeof(float));
    rb_head = (rb_head + chunk) % cap;
    rb_size += chunk;
    samples += chunk;
    to_write -= chunk;
  }

  // Soft cap: keep queued audio small to avoid audible delay
  if (rb_size > rb_soft_cap_samples) {
    const std::size_t drop = rb_size - rb_soft_cap_samples;
    rb_tail = (rb_tail + drop) % cap;
    rb_size -= drop;
  }
}

void RaylibFrontend::pump_audio() {
  if (!audio_ready)
    return;

#ifdef __EMSCRIPTEN__
  constexpr int max_refills_per_pump = 2;
#else
  constexpr int max_refills_per_pump = 3;
#endif

  int refills = 0;
  while (audio_prime > 0 || (refills < max_refills_per_pump &&
                             IsAudioStreamProcessed(audio_stream))) {

    constexpr std::size_t need = audio_chunk_frames * audio_channels; // floats
    std::size_t got = 0;
    constexpr std::size_t cap = ring_samples;

    while (got < need && rb_size > 0) {
      const std::size_t want = std::min(need - got, cap - rb_tail);
      const std::size_t take = std::min(want, rb_size);
      std::memcpy(&audio_tmp[got], &audio_rb[rb_tail], take * sizeof(float));
      rb_tail = (rb_tail + take) % cap;
      rb_size -= take;
      got += take;
    }

    if (got < need)
      std::fill(audio_tmp.begin() + got, audio_tmp.begin() + need, 0.0f);

    UpdateAudioStream(audio_stream, audio_tmp.data(), audio_chunk_frames);
    if (audio_prime > 0)
      --audio_prime;
    ++refills;
  }
}

void RaylibFrontend::tick_common(double dt_ms) {
  constexpr double cpu_hz = 4194304.0;
  constexpr std::size_t cycles_per_frame = 70224;
  constexpr std::size_t max_cycles_per_tick = cycles_per_frame * 4;

  dt_ms = std::clamp(dt_ms, 0.0, 100.0);

  cycle_accum += dt_ms * (cpu_hz / 1000.0);
  auto cycles_to_run = static_cast<std::size_t>(cycle_accum);
  cycles_to_run = std::min(cycles_to_run, max_cycles_per_tick);
  cycle_accum -= static_cast<double>(cycles_to_run);

  read_inputs();
  if (has_pending_savestate_request() && gbc->savestate_ready())
    process_pending_savestate_request();

  while (cycles_to_run) {
    const std::size_t block =
        std::min<std::size_t>(cycles_to_run, cycles_per_frame);
    advance_cycles_with_preemption(block);
    cycles_to_run -= block;
    pump_audio();
  }

  pump_audio();
  present();
}

#ifdef __EMSCRIPTEN__
void RaylibFrontend::restore_web_save() {
  auto *const bus = gbc ? gbc->get_bus() : nullptr;
  auto *const cart = bus ? bus->get_cartridge() : nullptr;
  if (!cart || !cart->has_battery())
    return;

  auto ram = cart->ram();
  if (ram.empty())
    return;

  (void)web_load_active_save(ram.data(), static_cast<int>(ram.size()));
  cart->consume_sram_save();
}

void RaylibFrontend::flush_web_save_now() {
  auto *const bus = gbc ? gbc->get_bus() : nullptr;
  auto *const cart = bus ? bus->get_cartridge() : nullptr;
  if (!cart || !cart->has_battery())
    return;

  const auto ram = cart->ram();
  if (ram.empty())
    return;

  (void)web_save_active_save(ram.data(), static_cast<int>(ram.size()));
  web_save_pending_flush = false;
  web_save_flush_deadline_ms = 0.0;
}

void RaylibFrontend::poll_web_save_persistence() {
  auto *const bus = gbc ? gbc->get_bus() : nullptr;
  auto *const cart = bus ? bus->get_cartridge() : nullptr;
  if (!cart || !cart->has_battery())
    return;

  if (cart->ram().empty())
    return;

  const double now_ms = emscripten_get_now();
  if (cart->consume_sram_save()) {
    web_save_pending_flush = true;
    web_save_flush_deadline_ms = now_ms + kWebSramFlushDebounceMs;
  }

  if (web_save_pending_flush && now_ms >= web_save_flush_deadline_ms)
    flush_web_save_now();
}

void RaylibFrontend::tick_web() {
  const double now_ms = emscripten_get_now();
  if (web_last_ms <= 0.0)
    web_last_ms = now_ms;
  double dt_ms = now_ms - web_last_ms;
  web_last_ms = now_ms;
  // Clamp to avoid huge catch-up bursts (e.g., background tab)
  dt_ms = std::clamp(dt_ms, 0.0, 100.0);

  tick_common(dt_ms);
  poll_web_save_persistence();
}
#endif

void RaylibFrontend::start() {
  InitWindow(fb_width * 4, fb_height * 4, "GBC");
  SetAudioStreamBufferSizeDefault(audio_chunk_frames);
  InitAudioDevice();

  audio_stream = LoadAudioStream(audio_sample_rate, 32, audio_channels);
  audio_ready = IsAudioStreamReady(audio_stream);
  if (audio_ready) {
#ifdef __EMSCRIPTEN__
    audio_prime = 0;
#else
    audio_prime = 0;
#endif
    PlayAudioStream(audio_stream);
    SetAudioStreamVolume(audio_stream, 0.5f);
  }

  Image img{};
  img.data = frame_buf.at(0).data();
  img.width = fb_width;
  img.height = fb_height;
  img.mipmaps = 1;
  img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  texture = LoadTextureFromImage(img);

  // Controls timing for both desktop and WASM builds
  constexpr auto target_fps = 60;
  SetTargetFPS(target_fps);

#ifdef __EMSCRIPTEN__
  restore_web_save();
  emscripten_set_main_loop_arg(frame_cb, this, 0, true);
#else

  // Desktop build is paced by using SetTargetFPS, nice and simple
  while (!WindowShouldClose()) {
    read_inputs();
    step_frame();
    pump_audio();
    present();
  }
#endif // __EMSCRIPTEN__
}
