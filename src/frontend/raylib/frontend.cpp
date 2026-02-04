#include "frontend/raylib/frontend.hpp"
#include "gbc.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include <raylib.h>
#include <stdexcept>

// We are doing this specifically to cut out the need for this flag:
//  -sASYNCIFY
// when building the WASM target, since it kills performance, it kinda
// sucks that it leads to implementing two emulation loops but eh.
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
static void frame_cb(void* user) {
    auto* fe = static_cast<RaylibFrontend*>(user);

    static double last_time = GetTime();
    double now = GetTime();
    double delta = now - last_time;

    constexpr double target_dt = 1.0 / 60.0;
    if (delta < target_dt)
        return;
    last_time = now;

    fe->read_inputs();
    fe->step_frame();
    fe->present();
}
#endif // __EMSCRIPTEN__

static std::uint32_t format_color(std::uint32_t c) {
  return ((c & 0x00FF0000) >> 16) | ((c & 0x0000FF00)) |
         ((c & 0x000000FF) << 16) | 0xFF000000;
}

RaylibFrontend::RaylibFrontend(const cart &c) { gbc->insert_cartridge(c); }

RaylibFrontend::~RaylibFrontend() {
  if (texture.id)
    ::UnloadTexture(texture);
  ::CloseWindow();
}

std::array<std::uint32_t, 144 * 160> RaylibFrontend::get_frame() {
  return frame_buf;
}

void RaylibFrontend::put_pixel(int x, int y, std::uint32_t c) {
  if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) [[unlikely]]
    return;
  frame_buf.at(y * fb_width + x) = format_color(c);
}

void RaylibFrontend::clear(std::uint32_t c) { frame_buf.fill(format_color(c)); }

void RaylibFrontend::read_inputs() {
  std::uint8_t input_state{};

  if (::IsKeyDown(KEY_UP))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::UP);
  if (::IsKeyDown(KEY_DOWN))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::DOWN);
  if (::IsKeyDown(KEY_LEFT))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::LEFT);
  if (::IsKeyDown(KEY_RIGHT))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::RIGHT);
  if (::IsKeyDown(KEY_Z))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::A);
  if (::IsKeyDown(KEY_X))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::B);
  if (::IsKeyDown(KEY_BACKSPACE))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::SELECT);
  if (::IsKeyDown(KEY_ENTER))
    input_state |= static_cast<std::uint8_t>(Joypad::JoypadButton::START);

  // This will never fail... Can't wait to eat these words though
  Joypad::JOYP *const joyp = static_cast<Joypad::JOYP *>(
      gbc->get_bus()->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  if (!joyp) [[unlikely]]
    throw std::runtime_error("RaylibFrontend::read_inputs()");
  joyp->set_state(input_state);
}

void RaylibFrontend::step_frame() {
  constexpr std::size_t cycles_per_frame = 70224;
  for (std::size_t i{0}; i < cycles_per_frame; i++)
    gbc->step();
}

void RaylibFrontend::present() {
  ::UpdateTexture(texture, frame_buf.data());
  ::BeginDrawing();
  ::ClearBackground(BLACK);
  ::DrawTexturePro(
      texture, Rectangle{0, 0, (float)fb_width, (float)fb_height},
      Rectangle{0, 0, (float)::GetScreenWidth(), (float)::GetScreenHeight()},
      Vector2{0, 0}, 0.0f, WHITE);
  ::EndDrawing();
}

void RaylibFrontend::start() {
  ::InitWindow(fb_width * 4, fb_height * 4, "GBC");

  Image img{};
  img.data = frame_buf.data();
  img.width = fb_width;
  img.height = fb_height;
  img.mipmaps = 1;
  img.format = ::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  texture = ::LoadTextureFromImage(img);

#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop_arg(frame_cb, this, 0, true);
#else

  constexpr auto target_fps = 60;
  ::SetTargetFPS(target_fps);

  // Desktop build is paced by using SetTargetFPS, nice and simple
  while (!::WindowShouldClose()) {
    read_inputs();
    step_frame();
    present();
  }
#endif // __EMSCRIPTEN
}
