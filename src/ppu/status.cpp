#include "ppu/status.hpp"
#include <cassert>

namespace PPU {

void STAT::write(byte_t value) {
  // Most significant bit is un-mapped
  state = value | 0x80;
}

byte_t STAT::read() {
  // Most significant bit is un-mapped
  return state | 0x80;
}

void LY::write(byte_t) { /* Read only */ }

byte_t LY::read() {
  assert(state >= 0 && state <= 153);
  return state;
}

}; // namespace PPU
