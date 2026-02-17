#ifndef GBC_PY_FRONTEND_HPP
#define GBC_PY_FRONTEND_HPP

#include "frontend/frontend.hpp"
#include <array>
#include <cstdint>

class PyFrontend final : public Frontend {
  using frame_buf_t = std::array<std::uint32_t, 160 * 144>;

public:
  void start() override {}
  PyFrontend();

  /**
   * Frame data manipulators
   */
  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c) override;
  frame_buf_t get_frame() override;

  /**
   * Audio manipulators: TODO
   */
  void queue_audio_samples(const float *, std::size_t) override {}

private:
  frame_buf_t frame_data{};
};

#endif // GBC_PY_FRONTEND_HPP
