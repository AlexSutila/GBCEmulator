#include "memory/mmio/cgb.hpp"
#include <cassert>

namespace PPU {

void VramBank::write(const byte_t value) { state = value | 0xFE; }
byte_t VramBank::read() { return state | 0xFE; }

/* Only bit 0 matters, all other bits are ignored. Pandoc claims that unused
 * MMIO bits (mostly) read 1 unless specified otherwise. */
const byte_t VramBank::get_bank() const { return state & 0x01; }

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
