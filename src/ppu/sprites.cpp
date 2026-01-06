#include "ppu/sprites.hpp"

/* These are helpers that determine if a sprite lies along a scanline, and will
 * ultimately decide if a row of pixels from a said sprite will be rendered or
 * not. To be used during OAM search specifically. */
bool sprite_visible(
    const byte_t x_pos,        // From object attribute memory
    const byte_t y_pos,        // From object attribute memory
    const byte_t cur_scanline, // Basically contents of LY register
    const byte_t cur_pixel)    // Where we're at in the scanline
{
  return false;
}
