#ifndef RAYLIB_FRONTEND_H
#define RAYLIB_FRONTEND_H

#include "frontend/frontend.hpp"
#include <array>
#include <cstdint>
#include <raylib.h>

struct cart;

class RaylibFrontend final : public Frontend {
public:
  explicit RaylibFrontend(const cart &c);
  ~RaylibFrontend();

  std::array<std::uint32_t, 144 * 160> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c) override;
  void start() override;

  void read_inputs();
  void step_frame();
  void present();

  // TODO
  void queue_audio_samples(const float *samples,
                           std::size_t sample_count) override {}

private:
  static constexpr int fb_height = 144;
  static constexpr int fb_width = 160;

  // This was double buffered at one point but WASM is a pain in my ass so
  std::array<std::uint32_t, 144 * 160> frame_buf{};
  ::Texture2D texture{};
};

#endif // RAYLIB_FRONTEND_H
