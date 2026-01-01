#ifndef __PIXEL_H
#define __PIXEL_H

#include "emu_types.hpp"

struct pixel {
  byte_t color; // A value between 0 and 3 (subject to change)
  bool discard;
};

#endif // __PIXEL_H
