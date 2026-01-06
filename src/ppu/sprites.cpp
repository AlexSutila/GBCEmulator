#include "ppu/sprites.hpp"

bool sprite_visible(const byte_t x_pos, const byte_t y_pos,
                    const byte_t cur_scanline, const byte_t cur_pixel) {
  // TODO
  return false;
}

/* Note, we still pass the X position here because AAAAAHGFDHGLSKHJG but also
 * because it's placed off super far right or left the sprite won't be rendered
 * and therefore doesn't need to be tracked during OAM search. */
bool sprite_visible(const byte_t x_pos, const byte_t y_pos,
                    const byte_t cur_scanline) {
  // TODO: Consider variable height sprites
  constexpr auto sprite_size_px = 8;

  return (cur_scanline >= y_pos) &&                 // Sprite upper bound
         (cur_scanline < y_pos + sprite_size_px) && // Sprite lower bound
         (x_pos == 0 || x_pos >= 168); // Visible on screen horizontally?
}
