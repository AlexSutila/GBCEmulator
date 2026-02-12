#ifndef RAYLIB_FRONTEND_H
#define RAYLIB_FRONTEND_H

#include "frontend/frontend.hpp"
#include <array>
#include <raylib.h>

struct cart;

class RaylibFrontend final : public Frontend {
public:
  explicit RaylibFrontend(const cart &c);
  ~RaylibFrontend() override;

  std::array<std::uint32_t, 144 * 160> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c) override;
  void start() override;

  void read_inputs() const;
  void step_frame() const;
  void present();

  // TODO: WASM doesn't like heap allocated floats?????
  void queue_audio_samples(const float *samples,
                           std::size_t sample_count) override {}

private:
  static constexpr auto fb_height = 144;
  static constexpr auto fb_width = 160;

  // We double buffer here, even though this is single threaded
  static constexpr auto nbuf = 2;
  std::size_t write_idx{0};
  std::size_t display_idx{0};
  bool frame_ready{false};

  // Double buffer, swap only when needed, prevents screen tears
  std::array<std::array<std::uint32_t, 144 * 160>, nbuf> frame_buf{};
  ::Texture2D texture{};
};

#endif // RAYLIB_FRONTEND_H
