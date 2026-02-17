#include "frontend/python/frontend.hpp"
#include <cstddef>

PyFrontend::PyFrontend() = default;

void PyFrontend::put_pixel(const int x, const int y, const std::uint32_t c) {
  static constexpr auto frame_width = 160;
  frame_data[y * frame_width + x] = c;
}

void PyFrontend::clear(const std::uint32_t c) {
  for (std::size_t i{0}; i < frame_data.size(); i++)
    frame_data.at(i) = c;
}

using frame_buf_t = std::array<std::uint32_t, 160 * 144>;
frame_buf_t PyFrontend::get_frame() { return frame_data; }
