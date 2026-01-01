#include "ppu/attributes.hpp"

byte_t get_bg_attrib_palette(byte_t attrib) {
  // CGB only: selects one of eight color palettes in CRAM
  return attrib & 0x7;
}

byte_t get_bg_attrib_bank(byte_t attrib) {
  // CGB only: selects which VRAM bank to fetch tile data from
  return ((attrib & 0x8) >> 3) & 0x1;
}
