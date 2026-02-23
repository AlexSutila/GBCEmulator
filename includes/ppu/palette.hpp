#ifndef GBC_PALETTE_HPP
#define GBC_PALETTE_HPP

#include "emu_types.hpp"
#include "memory/mmio/cgb.hpp"

#include <array>

namespace Savestate {
class Reader;
class Writer;
}

class ColorRam {
public:
  [[nodiscard]] std::uint32_t get_cgb_color(byte_t color_idx,
                                            byte_t palette_idx) const;
  PPU::PaletteData *get_data_reg();
  PPU::PaletteIdx *get_idx_reg();
  ColorRam();
  void savestate_serialize(Savestate::Writer &out) const;
  void savestate_deserialize(Savestate::Reader &in);

private:
  // Order here matters because C++ sucks
  std::array<byte_t, 64> mem_;
  PPU::PaletteIdx idx_reg;
  PPU::PaletteData data_reg;
};

/* TODO: This is currently not in use, however I'm leaving the support for it
 * anyway. It would be cool to offer a togglable "additional" compatability
 * option that bypasses the coloring the CGB hardware does for DMG games. */
[[nodiscard]] std::uint32_t get_mono_color(byte_t idx);

/* Helpers for color format conversion */
[[nodiscard]] std::uint16_t argb8888_to_rgb555(std::uint32_t argb);
[[nodiscard]] std::uint32_t rgb555_to_argb8888(byte_t, byte_t);

#endif // GBC_PALETTE_HPP
