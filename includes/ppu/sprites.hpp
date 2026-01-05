#ifndef __SPRITE_H
#define __SPRITE_H

#include "emu_types.hpp"

/* Term `Sprite` is interchangeable with `Object` in OAM. */
struct Sprite {
  byte_t y_pos;
  byte_t x_pos;
  byte_t tile_idx;
  byte_t tile_attr;

  // For storing in containers, in case we end up doing that
  bool operator<(const Sprite &other) const noexcept {
    return x_pos > other.x_pos;
  }
};

#endif // __SPRITE_H
