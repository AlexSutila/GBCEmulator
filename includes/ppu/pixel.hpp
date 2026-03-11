#ifndef GBC_PIXEL_HPP
#define GBC_PIXEL_HPP

#include "emu_types.hpp"

struct pixel {
  byte_t color_idx;   // A value between 0 and 3
  byte_t palette_idx; // A value between 0 and 7 (CGB mode only)

  /* This is required for sprite pixels. The DMG model always renders sprites
   * which appear earlier on the X-axis on top. This behavior can optionally
   * be swapped out in CGB mode to prioritize based on OAM index instead. */
  byte_t oam_index;

  /* Is used by both sprite and background pixels to resolve priority conflicts.
   * This flag will always represent what was sampled from the tile (or sprite)
   * attribute byte. */
  bool take_priority;
};

// NOTE: Only applicable to object/sprite pixels in either CGB or DMG modes
[[nodiscard]] inline bool is_transparent(const byte_t color_idx) { return color_idx == 0; }
[[nodiscard]] inline bool is_transparent(const pixel &px) { return px.color_idx == 0; }

#endif // GBC_PIXEL_HPP
