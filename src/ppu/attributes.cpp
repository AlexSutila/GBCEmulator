#include "ppu/attributes.hpp"

byte_t do_y_px_flip(const byte_t y_px, bool flip) {
  constexpr byte_t max_pixel_idx = 7, pixel_mask = 0x7;
  if (flip)
    return max_pixel_idx - (y_px & pixel_mask);
  return y_px & pixel_mask;
}

byte_t calc_color_idx(const byte_t lo_byte,  // Low data byte
                      const byte_t hi_byte,  // High data byte
                      std::size_t pixel_idx, // Which pixel?
                      bool flip)             // Decides flip
{
  byte_t hi_bit{}, lo_bit{};
  if (flip) {
    hi_bit = (hi_byte & (0x01 << pixel_idx)) != 0 ? 1 : 0;
    lo_bit = (lo_byte & (0x01 << pixel_idx)) != 0 ? 1 : 0;
  } else {
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

bool get_bg_attrib_priority(byte_t attrib) {
  // CGB only: fight over priority, logic is convoluted lol
  return ((attrib & 0x80) >> 7) & 0x1;
}

byte_t get_obj_attrib_dmg_palette(byte_t attrib) {
  // Non CGB only: Determiens between palette selection between OBJ0 and OBJ1
  return ((attrib & 0x10) >> 4) & 0x1;
}

byte_t get_obj_attrib_bank(byte_t attrib) {
  // CGB only: Get VRAM bank source for fetching tile data
  return ((attrib & 0x08) >> 3) & 0x1;
}

byte_t get_obj_attrib_cgb_palette(byte_t attrib) {
  // CGB only: Get CGB palette index
  return attrib & 0x7;
}

bool get_obj_attrib_x_flip(byte_t attrib) {
  // Determines if tiles are flipped horizontally
  return ((attrib & 0x20) >> 5) & 0x1;
}

bool get_obj_attrib_y_flip(byte_t attrib) {
  // Determines if tiles are flipped vertically
  return ((attrib & 0x40) >> 6) & 0x1;
}
bool get_obj_attrib_priority(byte_t attrib) {
  // Fight over render priority, logic is convoluted
  return ((attrib & 0x80) >> 7) & 0x1;
}
