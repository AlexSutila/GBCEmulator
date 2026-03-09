#include "ppu/palette.hpp"
#include "emu_types.hpp"
#include "memory/mmio/cgb.hpp"
#include "savestate/codec.hpp"
#include <cstdint>

std::uint16_t argb8888_to_rgb555(const std::uint32_t argb) {
  const std::uint8_t r8 = (argb >> 16) & 0xFF;
  const std::uint8_t g8 = (argb >> 8) & 0xFF;
  const std::uint8_t b8 = argb & 0xFF;

  // Truncate the lower bits
  const std::uint8_t r5 = r8 >> 3;
  const std::uint8_t g5 = g8 >> 3;
  const std::uint8_t b5 = b8 >> 3;
  return static_cast<std::uint16_t>((r5 << 0) | (g5 << 5) | (b5 << 10));
}

std::uint32_t rgb555_to_argb8888(const std::uint8_t lo, const std::uint8_t hi) {
  const std::uint16_t rgb555 =
      (static_cast<std::uint16_t>(hi) << 8) | static_cast<std::uint16_t>(lo);

  const std::uint8_t r5 = (rgb555 >> 0) & 0x1F;
  const std::uint8_t g5 = (rgb555 >> 5) & 0x1F;
  const std::uint8_t b5 = (rgb555 >> 10) & 0x1F;

  // Expand the lower bits
  const std::uint8_t r8 = (r5 << 3) | (r5 >> 2);
  const std::uint8_t g8 = (g5 << 3) | (g5 >> 2);
  const std::uint8_t b8 = (b5 << 3) | (b5 >> 2);
  return (0xFFu << 24) | (static_cast<std::uint32_t>(r8) << 16) |
         (static_cast<std::uint32_t>(g8) << 8) | static_cast<std::uint32_t>(b8);
}

/* Initialization order matters because the data register has internal
 * dependencies on both the RAM array and the index register. */
ColorRam::ColorRam() : mem_{}, idx_reg(), data_reg(mem_, idx_reg) {}

enum : std::uint16_t {
  F_MEM,
  F_IDX,
  F_DATA,
};

template <typename T> void ColorRam::parse_savestate(T &t) {
  constexpr auto version = 1;
  t.chunk_header(version, Savestate::C_CRAM);
  t.field_bytes(F_MEM, mem_);
  t.field_complex(F_IDX, [&](T &t) { idx_reg.parse_savestate(t); });
  t.field_complex(F_DATA, [&](T &t) { data_reg.parse_savestate(t); });
  t.eof();
}

template void ColorRam::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void ColorRam::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void ColorRam::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void
ColorRam::parse_savestate<Savestate::Checker>(Savestate::Checker &);

PPU::PaletteData *ColorRam::get_data_reg() { return &data_reg; }
PPU::PaletteIdx *ColorRam::get_idx_reg() { return &idx_reg; }

/*
 * Each palette color is stored as a 16-bit little-endian RGB555 value:
 *
 *   Bit:   15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 *          -  B4 B3 B2 B1 B0 G4 G3 G2 G1 G0 R4 R3 R2 R1 R0
 *
 *   R (bits 0–4)   : Red intensity
 *   G (bits 5–9)   : Green intensity
 *   B (bits 10–14) : Blue intensity
 *
 * Bit 15 is unused, but I believe it is still readable/writable.
 */
std::uint32_t ColorRam::get_cgb_color(const byte_t color_idx,
                                      const byte_t palette_idx) const {
  constexpr byte_t bytes_per_palette = 8;
  constexpr byte_t bytes_per_color = 2;

  // Extract the low and high bytes of the color data
  const addr_t base_addr =
      (palette_idx * bytes_per_palette) + (color_idx * bytes_per_color);
  const byte_t lo = mem_.at(base_addr);
  const byte_t hi = mem_.at(base_addr + 1);

  // Compute the full color value, and convert it from RGB555 format
  return rgb555_to_argb8888(lo, hi);
}

/* Not in use (see comment under palette.hpp), but these are original colors
 * that can be used for true DMG monochrome. */
static constexpr std::uint32_t mono_pal[4] = {
    0xFFFFFFFF, // white
    0xFFAAAAAA, // light-grey
    0xFF555555, // dark-grey
    0xFF000000  // black
};

std::uint32_t get_mono_color(const byte_t idx) { return mono_pal[idx & 0x7]; }
