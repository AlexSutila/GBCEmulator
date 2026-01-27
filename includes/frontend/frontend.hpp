#ifndef __FRONTEND_H
#define __FRONTEND_H

#include "gbc.hpp"
#include <memory>
#include <array>

class Frontend {
public:
  explicit Frontend() : gbc(std::make_unique<GameBoyColor>(*this)) {}
  virtual std::array<std::uint32_t, 160 * 144> get_frame() = 0;
  virtual void put_pixel(int x, int y, std::uint32_t c) = 0;
  virtual void clear(std::uint32_t c = 0x00FFFFFF) = 0;
  virtual void queue_audio_samples(const float *samples,
                                 std::size_t sample_count) = 0;
  virtual void start() = 0;

  std::unique_ptr<GameBoyColor> &get() { return gbc; }

protected:
  std::unique_ptr<GameBoyColor> gbc;
};

#endif // __FRONTEND_H
