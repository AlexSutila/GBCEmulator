#ifndef __PIXEL_H
#define __PIXEL_H

#include "emu_types.hpp"

struct pixel {
  byte_t color_idx;   // A value between 0 and 3
  byte_t palette_idx; // A value between 0 and 7 (CGB mode only)
  bool take_priority; // Was the priority bit set?
  bool discard;
};

[[nodiscard]] inline bool is_transparent(const pixel &px) {
  // NOTE: Only applicable to object/sprite pixels
  return px.color_idx == 0;
}

#endif // __PIXEL_H
