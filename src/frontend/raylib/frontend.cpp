#include "frontend/raylib/frontend.hpp"
#include "gbc.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include <atomic>
#include <chrono>
#include <raylib.h>
#include <stdexcept>
#include <thread>

static std::uint32_t format_color(std::uint32_t c) {
  return ((c & 0x00FF0000) >> 16) | ((c & 0x0000FF00)) |
         ((c & 0x000000FF) << 16) | 0xFF000000;
}

RaylibFrontend::RaylibFrontend(const cart &c) { gbc->insert_cartridge(c); }

RaylibFrontend::~RaylibFrontend() {
  running.store(false, std::memory_order_release);
  if (emu_thread.joinable())
    emu_thread.join();
  if (texture.id)
    UnloadTexture(texture);
  CloseWindow();
}

std::array<std::uint32_t, 144 * 160> RaylibFrontend::get_frame() {
  return frame_buf.at(front_idx.load(std::memory_order_acquire));
}

void RaylibFrontend::put_pixel(int x, int y, std::uint32_t c) {
  if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) [[unlikely]]
    return;

  // Double buffering, need to access oposite buffer
  const std::size_t fb_idx = front_idx.load(std::memory_order_relaxed);
  const auto nbuf = frame_buf.size();
  frame_buf.at((fb_idx + 1) % nbuf).at(y * fb_width + x) = format_color(c);

  // Swap buffers to avoid screen tear
  if (x == fb_width - 1 && y == fb_height - 1) {
    front_idx.store((fb_idx + 1) % nbuf, std::memory_order_release);
    frame_ready.store(true, std::memory_order_release);
  }
}

void RaylibFrontend::clear(std::uint32_t c) {
  for (auto &buf : frame_buf)
    buf.fill(format_color(c));
  front_idx.store(0, std::memory_order_release);
}

void RaylibFrontend::emulation_loop() {
  constexpr int cycles_per_frame = 70224, syncs_per_frame = 4;
  constexpr int sync_cycles = cycles_per_frame / syncs_per_frame;

  // Target sync time per period (microseconds), intentionally sub-frame
  constexpr double frame_time_sec = 1.0 / 60.0;
  constexpr auto sync_time_us =
      static_cast<int>((frame_time_sec / syncs_per_frame) * 1'000'000);
  using clock = std::chrono::high_resolution_clock;

  // The real emulation loop begins
  while (running.load(std::memory_order_relaxed)) {
    auto start = clock::now();
    for (int i = 0; i < sync_cycles; ++i)
      gbc->step();

    // Time so it doesn't max out the CPU
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        clock::now() - start);
    if (elapsed.count() < sync_time_us)
      std::this_thread::sleep_for(
          std::chrono::microseconds(sync_time_us - elapsed.count()));
    read_inputs();
  }
}

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

void RaylibFrontend::present() {
  const std::size_t fb_idx = front_idx.load(std::memory_order_acquire);
  const auto &front = frame_buf.at(fb_idx);
  ::UpdateTexture(texture, front.data());
  ::BeginDrawing();
  ::ClearBackground(BLACK);

  ::DrawTexturePro(
      texture, Rectangle{0, 0, (float)fb_width, (float)fb_height},
      Rectangle{0, 0, (float)::GetScreenWidth(), (float)::GetScreenHeight()},
      Vector2{0, 0}, 0.0f, WHITE);
  ::EndDrawing();
}

void RaylibFrontend::start() {
  InitWindow(fb_width * 4, fb_height * 4, "GBC");
  Image img{};
  img.data = frame_buf.at(0).data();
  img.width = fb_width;
  img.height = fb_height;
  img.mipmaps = 1;
  img.format = ::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

  texture = ::LoadTextureFromImage(img);
  running.store(true, std::memory_order_release);
  emu_thread = std::thread(&RaylibFrontend::emulation_loop, this);

  while (!WindowShouldClose()) {
    if (frame_ready.exchange(false, std::memory_order_acq_rel))
      present();
    else
      ::WaitTime(0.001);
  }
  running.store(false, std::memory_order_release);
}
