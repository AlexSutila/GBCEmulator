#ifndef __FRONTEND_H
#define __FRONTEND_H

#include "gbc.hpp"
#include <cstdint>
#include <memory>

class Frontend {
public:
  explicit Frontend() : gbc_(std::make_unique<GameBoyColor>(*this)) {}
  virtual void put_pixel(int x, int y, std::uint32_t c) = 0;
  virtual void clear() = 0;
  virtual void start() = 0;
  std::unique_ptr<GameBoyColor> &get() { return gbc_; }

protected:
  std::unique_ptr<GameBoyColor> gbc_;
};

#endif // __FRONTEND_H
