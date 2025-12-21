#include "ppu/status.hpp"
#include "emu_types.hpp"
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

const StatModes STAT::get_mode() const {
  constexpr byte_t mode_mask = 0x03;
  const StatModes mode = static_cast<StatModes>(state & mode_mask);
  return mode;
}

void STAT::set_mode(StatModes mode) {
  constexpr byte_t mode_mask = 0x03;
  const byte_t mode_bits = static_cast<byte_t>(mode);
  state = state & ~mode_mask;
  state |= mode_bits;
}

void LY::write(byte_t) { /* Read only */ }

byte_t LY::read() {
  assert(state >= 0 && state <= max_ly());
  return state;
}

bool LY::inc() {
  if (state == max_ly()) {
    state = 0;
    return true;
  }
  ++state;
  return false;
}

}; // namespace PPU
