#include "memory/mmio/cgb.hpp"
#include <cassert>

namespace PPU {

void VramBank::write(const byte_t value) { state = value | 0xFE; }
byte_t VramBank::read() { return state | 0xFE; }

/* Only bit 0 matters, all other bits are ignored. Pandoc claims that unused
 * MMIO bits (mostly) read 1 unless specified otherwise. */
const byte_t VramBank::get_bank() const { return state & 0x01; }

void PaletteIdx::write(const byte_t value) {
  // Fourth bit is unused
  state = value | 0x40;
}
byte_t PaletteIdx::read() { return state; }

/* Writes to color RAM can increase the value stored in this register */
void PaletteIdx::inc() {
  const byte_t upper_bits = state & ~0x7F; // Careful: Include unused bit
  const byte_t addr_bits = (state + 1) & 0x3F;
  state = upper_bits | addr_bits;
}
bool PaletteIdx::auto_inc_enabled() const { return (state & 0x80) != 0; }

/* Obtains the address used to index Color RAM */
addr_t PaletteIdx::get_address() const { return state & 0x3F; }

/* It kinda sucks, but this ends up needing a reference to the underlying color
 * RAM memory block that it reads from, and the corresponding index register. */
PaletteData::PaletteData(std::array<byte_t, 64> &mem, PaletteIdx &idx)
    : mem_(mem), idx_(idx) {}

/* Writes to PaletteData registers also have the opportunity to increment their
 * corresponding PaletteIndex register. */
void PaletteData::write(const byte_t value) {
  const addr_t addr = idx_.get_address() & 0x3F;
  if (idx_.auto_inc_enabled())
    idx_.inc();
  mem_.at(addr) = value;
}
byte_t PaletteData::read() {
  const addr_t addr = idx_.get_address() & 0x3F;
  return mem_.at(addr);
}

} // namespace PPU

void WramBank::write(const byte_t value) { state = value | 0xF8; }
byte_t WramBank::read() { return state | 0xF8; }

/* Only bits 0-2 matter, and only values 1-7 actually map to their respective
 * banks. If zero is written, it will map to bank 1, as bank 0 can always be
 * used from the 0xC000-0xCFFF address range. */
const byte_t WramBank::get_bank() const {
  byte_t ret = state & 0x07; // Only read bits 0-2
  if (ret == 0)
    ++ret;
  return ret;
}
