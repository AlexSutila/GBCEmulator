#ifndef __PIXEL_H
#define __PIXEL_H

#include "emu_types.hpp"

struct pixel {
  byte_t color_idx; // A value between 0 and 3
  byte_t palette;   // A value between 0 and 7 (CGB mode only)
  bool discard;
};

#endif // __PIXEL_H
