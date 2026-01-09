#ifndef __FRONTEND_H
#define __FRONTEND_H

#include "gbc.hpp"
#include <cstdint>
#include <memory>

class Frontend {
public:
  explicit Frontend() : gbc_(std::make_unique<GameBoyColor>()) {}
  virtual void put_pixel(int x, int y, std::uint32_t c) = 0;
  virtual void clear() = 0;
  virtual void start() = 0;

private:
  std::unique_ptr<GameBoyColor> gbc_;
};

#endif // __FRONTEND_H
