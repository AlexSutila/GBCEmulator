#ifndef __PALETTE_H
#define __PALETTE_H

#include "emu_types.hpp"
#include "memory/mmio/cgb.hpp"

#include <array>
#include <cstdint>

class ColorRam {
public:
  const std::uint32_t get_cgb_color(const byte_t color_idx,
                                    const byte_t palette_idx) const;
  PPU::PaletteData *const get_data_reg();
  PPU::PaletteIdx *const get_idx_reg();
  ColorRam();

private:
  // Order here matters because C++ sucks
  std::array<byte_t, 64> mem_;
  PPU::PaletteIdx idx_reg;
  PPU::PaletteData data_reg;
};

[[nodiscard]] const std::uint32_t get_mono_color(const byte_t idx);

#endif // __PALETTE_H
