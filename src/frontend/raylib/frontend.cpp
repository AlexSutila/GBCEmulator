#include "frontend/raylib/frontend.hpp"
#include "gbc.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include <cstring>
#include <raylib.h>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

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

static std::uint32_t format_color(const std::uint32_t c) {
  return ((c & 0x00FF0000) >> 16) | ((c & 0x0000FF00)) |
         ((c & 0x000000FF) << 16) | 0xFF000000;
}

RaylibFrontend::RaylibFrontend(const cart &c) { gbc->insert_cartridge(c); }

RaylibFrontend::~RaylibFrontend() {
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

void RaylibFrontend::read_controller_inputs(std::uint8_t &input_state) const {
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP))
    input_state |= (std::uint8_t)Joypad::JoypadButton::UP;
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
    input_state |= (std::uint8_t)Joypad::JoypadButton::DOWN;
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))
    input_state |= (std::uint8_t)Joypad::JoypadButton::LEFT;
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
    input_state |= (std::uint8_t)Joypad::JoypadButton::RIGHT;
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))
    input_state |= (std::uint8_t)Joypad::JoypadButton::START;
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT))
    input_state |= (std::uint8_t)Joypad::JoypadButton::SELECT;

  // Since the right face may have multiple buttons, bind multiple to a single
  // virtual key. Better to have options.
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
      IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))
    input_state |= (std::uint8_t)Joypad::JoypadButton::A;
  if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
      IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP))
    input_state |= (std::uint8_t)Joypad::JoypadButton::B;
}

void RaylibFrontend::read_keyboard_inputs(std::uint8_t &input_state) const {
  if (IsKeyDown(KEY_UP))
    input_state |= (std::uint8_t)Joypad::JoypadButton::UP;
  if (IsKeyDown(KEY_DOWN))
    input_state |= (std::uint8_t)Joypad::JoypadButton::DOWN;
  if (IsKeyDown(KEY_LEFT))
    input_state |= (std::uint8_t)Joypad::JoypadButton::LEFT;
  if (IsKeyDown(KEY_RIGHT))
    input_state |= (std::uint8_t)Joypad::JoypadButton::RIGHT;
  if (IsKeyDown(KEY_Z))
    input_state |= (std::uint8_t)Joypad::JoypadButton::A;
  if (IsKeyDown(KEY_X))
    input_state |= (std::uint8_t)Joypad::JoypadButton::B;
  if (IsKeyDown(KEY_BACKSPACE))
    input_state |= (std::uint8_t)Joypad::JoypadButton::SELECT;
  if (IsKeyDown(KEY_ENTER))
    input_state |= (std::uint8_t)Joypad::JoypadButton::START;
}

void RaylibFrontend::read_inputs() const {
  std::uint8_t input_state{};

#ifdef __EMSCRIPTEN__
  // On web builds, keyboard input is handled in JS so it can be rebound.
  input_state = g_web_input_state;
#else
  read_keyboard_inputs(input_state);
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

void RaylibFrontend::step_frame() const {
  constexpr std::size_t cycles_per_frame = 70224;
  for (std::size_t i{0}; i < cycles_per_frame; i++)
    gbc->step();
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

  while (cycles_to_run) {
    const std::size_t block =
        std::min<std::size_t>(cycles_to_run, cycles_per_frame);
    for (std::size_t i = 0; i < block; i++)
      gbc->step();
    cycles_to_run -= block;
    pump_audio();
  }

  pump_audio();
  present();
}

#ifdef __EMSCRIPTEN__
void RaylibFrontend::tick_web() {
  const double now_ms = emscripten_get_now();
  if (web_last_ms <= 0.0)
    web_last_ms = now_ms;
  double dt_ms = now_ms - web_last_ms;
  web_last_ms = now_ms;
  // Clamp to avoid huge catch-up bursts (e.g., background tab)
  dt_ms = std::clamp(dt_ms, 0.0, 100.0);

  tick_common(dt_ms);
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
