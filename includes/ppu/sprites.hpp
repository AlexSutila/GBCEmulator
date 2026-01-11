#ifndef __SPRITE_H
#define __SPRITE_H

#include "emu_types.hpp"
#include <cstddef>

static constexpr addr_t oam_y_offset = 0;
static constexpr addr_t oam_x_offset = 1;
static constexpr addr_t oam_tile_idx_offset = 2;
static constexpr addr_t oam_attr_offset = 3;
static constexpr addr_t sprite_size_bytes = 4;

// Term `Sprite` is interchangeable with `Object` in OAM
struct Sprite {

  // Sprite attributes
  byte_t y_pos;
  byte_t x_pos;
  byte_t tile_idx;
  byte_t tile_attr;

  // Actual index in object attribute memory
  std::size_t obj_no;

  // For storing in containers, in case we end up doing that
  bool operator<(const Sprite &other) const noexcept {
    return x_pos > other.x_pos;
  }
};

/* These are helpers that determine if a sprite lies along a scanline, and will
 * ultimately decide if a row of pixels from a said sprite will be rendered or
 * not. */
[[nodiscard]] bool // Helper for exact pixel position during rendering phase
sprite_visible(const byte_t x_pos,        // From object attribute memory
               const byte_t cur_pixel);   // Where we're at in the scanline
[[nodiscard]] bool // Helper for whole scanline checks during OAM memory scan
sprite_visible(const byte_t x_pos,         // From object attribute memory
               const byte_t y_pos,         // From object attribute memory
               const byte_t cur_scanline); // Basically contents of LY register

#endif // __SPRITE_H
