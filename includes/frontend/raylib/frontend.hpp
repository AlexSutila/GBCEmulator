#ifndef RAYLIB_FRONTEND_H
#define RAYLIB_FRONTEND_H

#include "frontend/frontend.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <raylib.h>
#include <thread>

struct cart;

class RaylibFrontend final : public Frontend {
public:
  explicit RaylibFrontend(const cart &c);
  ~RaylibFrontend();

  std::array<std::uint32_t, 144 * 160> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c) override;
  void start() override;

  // TODO
  void queue_audio_samples(const float *samples,
                           std::size_t sample_count) override {}

private:
  static constexpr int fb_height = 144;
  static constexpr int fb_width = 160;

  std::array<std::array<std::uint32_t, 144 * 160>, 2> frame_buf{};
  std::atomic<std::size_t> front_idx{0};
  ::Texture2D texture{};

  std::atomic<bool> frame_ready{false};
  std::atomic<bool> running{false};
  std::thread emu_thread;

  void emulation_loop();
  void read_inputs();
  void present();
};

#endif // RAYLIB_FRONTEND_H
