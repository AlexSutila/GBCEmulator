#include "ppu/sprites.hpp"

bool sprite_visible(const byte_t x_pos, const byte_t cur_pixel) {
  return (cur_pixel + 8 >= x_pos) && (cur_pixel < x_pos);
}

/* Note, we still pass the X position here because AAAAAHGFDHGLSKHJG but also
 * because it's placed off super far right or left the sprite won't be rendered
 * and therefore doesn't need to be tracked during OAM search. */
bool sprite_visible(const byte_t x_pos, const byte_t y_pos,
                    const byte_t cur_scanline) {
  // TODO: Consider variable height sprites
  constexpr auto sprite_size_px = 8;

  // The edges of either sprite cut off at these values, there needs to be room
  // for them to be hidden off screen. These values come straight off pandocs.
  if (x_pos == 0 || x_pos >= 168)
    return false;

  // Top of any sprite becomes visible at `y_pos == 16` to allow for sprites
  // being placed off screen, hidden away physically above the LCD viewport.
  return (cur_scanline + 16 >= y_pos) &&
         (cur_scanline + 16 < y_pos + sprite_size_px);
}
