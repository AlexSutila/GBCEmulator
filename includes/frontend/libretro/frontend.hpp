#ifndef GBC_LIBRETRO_FRONTEND_HPP
#define GBC_LIBRETRO_FRONTEND_HPP

#include "frontend/frontend.hpp"
#include "libretro.h"
#include <array>
#include <cstddef>

class LibretroFrontend : public Frontend {
public:
  LibretroFrontend(const LibretroFrontend &) = delete;
  LibretroFrontend &operator=(const LibretroFrontend &) = delete;
  ~LibretroFrontend();

  static LibretroFrontend &getInstance() {
    static LibretroFrontend instance;
    return instance;
  };

  struct LibretroMeta {
    retro_video_refresh_t video_cb;
    retro_audio_sample_t audio_cb;
    retro_audio_sample_batch_t audio_batch_cb;
    retro_environment_t environ_cb;
    retro_input_poll_t input_poll_cb;
    retro_input_state_t input_state_cb;
  };
  LibretroMeta &get_libretro_meta() { return libretro; }

  std::array<std::uint32_t, 144 * 160> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c) override;
  void start() override;

  void present();

  void queue_audio_samples(const float *samples,
                           std::size_t sample_count) override;

private:
  static constexpr auto fb_height = 144;
  static constexpr auto fb_width = 160;

  static constexpr auto nbuf = 2;
  std::array<std::array<std::uint32_t, 144 * 160>, nbuf> frame_buf{};
  std::size_t write_idx{0}, display_idx{0};
  bool frame_ready{false};

  // Keep private for singleton design pattern
  explicit LibretroFrontend();
  LibretroMeta libretro{};
};

#endif // GBC_LIBRETRO_FRONTEND_HPP
