#include "ppu/palette.hpp"
#include "emu_types.hpp"
#include "memory/mmio/cgb.hpp"

#include <cstdint>

/* The monochrome color palette only used by games running in DMG mode. */
static constexpr std::uint32_t mono_pal[4] = {
    0xFFFFFFFF, // white
    0xFFAAAAAA, // light-grey
    0xFF555555, // dark-grey
    0xFF000000  // black
};

/* Initialization order matters because the data register has internal
 * dependencies on both the RAM array and the index register. */
ColorRam::ColorRam() : mem_{}, idx_reg(), data_reg(mem_, idx_reg) {}

const std::uint32_t get_mono_color(const byte_t idx) {
  /* TODO: These are still place holder values I just kinda threw in here for
   * the sake of getting the palettes looking good. It would be super nice if
   * we matched the values the color uses for backwards compatability. */
  return mono_pal[idx & 0x7];
}
