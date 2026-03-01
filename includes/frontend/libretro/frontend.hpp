#ifndef GBC_LIBRETRO_FRONTEND_HPP
#define GBC_LIBRETRO_FRONTEND_HPP

#include "frontend/frontend.hpp"
#include "libretro.h"
#include "memory/mmio/dmg.hpp"
#include <array>
#include <cstddef>

class LibretroFrontend : public Frontend {
public:
  LibretroFrontend(const LibretroFrontend &) = delete;
  LibretroFrontend &operator=(const LibretroFrontend &) = delete;
  ~LibretroFrontend();

  static LibretroFrontend &get_instance() {
    static LibretroFrontend instance;
    return instance;
  };

  struct LibretroCallbacks {
    retro_video_refresh_t video_cb;
    retro_audio_sample_t audio_cb;
    retro_audio_sample_batch_t audio_batch_cb;
    retro_environment_t environ_cb;
    retro_input_poll_t input_poll_cb;
    retro_input_state_t input_state_cb;
  };
  LibretroCallbacks &get_callbacks() { return cb; }

  struct LibretroMeta {
    unsigned controller_device{RETRO_DEVICE_JOYPAD};
  };
  LibretroMeta &get_meta() { return meta; }

  std::array<std::uint32_t, 144 * 160> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c) override;
  void start() override;

  void try_show_frame();
  void try_poll_input();

  void queue_audio_samples(const float *samples,
                           std::size_t sample_count) override;

private:
  static constexpr auto fb_height = 144;
  static constexpr auto fb_width = 160;
  static constexpr auto nbuf = 2;

  // Input polling and helpers
  bool test_input(unsigned id) const;
  Joypad::JOYP *joyp{nullptr};

  // Double-buffered frame metadata (even libretro suggests a double-buffer)
  std::array<std::array<std::uint32_t, 144 * 160>, nbuf> frame_buf{};
  std::size_t write_idx{0}, display_idx{0};
  bool frame_ready{false};

  // Audio metadata
  static constexpr unsigned audio_sample_rate = 44100;
  std::vector<std::int16_t> audio_buffer;

  // Libretro specific metadata and stuff
  LibretroCallbacks cb{};
  LibretroMeta meta{};

  // Keep private for singleton design pattern
  explicit LibretroFrontend();
};

#endif // GBC_LIBRETRO_FRONTEND_HPP
