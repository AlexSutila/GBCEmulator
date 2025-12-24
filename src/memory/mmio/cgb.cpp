#include "memory/mmio/cgb.hpp"

void VramBank::write(const byte_t value) { state = value | 0xFE; }
byte_t VramBank::read() { return state | 0xFE; }

/* Only bit 0 matters, all other bits are ignored. Pandoc claims that unused
 * MMIO bits (mostly) read 1 unless specified otherwise. */
const byte_t VramBank::get_bank() const { return state & 0x01; }
