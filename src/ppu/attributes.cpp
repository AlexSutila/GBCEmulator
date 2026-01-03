#include "ppu/attributes.hpp"

byte_t calc_color_idx(const byte_t lo_byte,  // Low data byte
                      const byte_t hi_byte,  // High data byte
                      std::size_t pixel_idx, // Which pixel?
                      const byte_t attr)     // Decides flip
{
  byte_t hi_bit{}, lo_bit{};

  if (get_bg_attrib_x_flip(attr)) {
    hi_bit = (hi_byte & (0x01 << pixel_idx)) != 0 ? 1 : 0;
    lo_bit = (lo_byte & (0x01 << pixel_idx)) != 0 ? 1 : 0;
  }

  else {
    hi_bit = (hi_byte & (0x80 >> pixel_idx)) != 0 ? 1 : 0;
    lo_bit = (lo_byte & (0x80 >> pixel_idx)) != 0 ? 1 : 0;
  }
  return (hi_bit << 1) | lo_bit;
}

byte_t get_bg_attrib_palette(byte_t attrib) {
  // CGB only: selects one of eight color palettes in CRAM
  return attrib & 0x7;
}

byte_t get_bg_attrib_bank(byte_t attrib) {
  // CGB only: selects which VRAM bank to fetch tile data from
  return ((attrib & 0x8) >> 3) & 0x1;
}

bool get_bg_attrib_x_flip(byte_t attrib) {
  // CGB only: determines if tiles are flipped horizontally
  return ((attrib & 0x20) >> 5) & 0x1;
}

bool get_bg_attrib_y_flip(byte_t attrib) {
  // CGB only: determines if tiles are flipped vertically
  return ((attrib & 0x40) >> 6) & 0x1;
}
