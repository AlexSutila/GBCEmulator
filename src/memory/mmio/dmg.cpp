#include "memory/mmio/dmg.hpp"
#include "emu_types.hpp"
#include "timer/timer.hpp"
#include <cassert>

namespace PPU {

/* LCD Control helpers */
const bool LCDCtrl::lcd_enabled() const { return (state & 0x80) != 0; }
const TileMapArea LCDCtrl::win_tilemap_base() const {
  return (state & 0x40) != 0 ? TileMapArea::HI_TILEMAP_BASE
                             : TileMapArea::LO_TILEMAP_BASE;
}
const bool LCDCtrl::win_enabled() const { return (state & 0x20) != 0; }
const TileDataArea LCDCtrl::bg_win_data_area() const {
  return (state & 0x10) != 0 ? TileDataArea::HI_TILEDATA_BASE
                             : TileDataArea::LO_TILEDATA_BASE;
}
const TileMapArea LCDCtrl::bg_tilemap_base() const {
  return (state & 0x08) != 0 ? TileMapArea::HI_TILEMAP_BASE
                             : TileMapArea::LO_TILEMAP_BASE;
}
const bool LCDCtrl::bg_win_en_priority() const { return (state & 0x1) != 0; }

/* The return value here will always be in reference to the height (pixels) of
 * the sprites. Sprites will never not be 8 pixels wide. */
const SpriteHeight LCDCtrl::obj_size() const {
  return (state & 0x04) != 0 ? SpriteHeight::TALL_SPRITES
                             : SpriteHeight::SHORT_SPRITES;
}
const bool LCDCtrl::obj_enable() const { return (state & 0x02) != 0; }

void STAT::write(byte_t value) {
  // Most significant bit is un-mapped, preserve mode bits
  state = (state & 0x03) | (value & 0x7C) | 0x80;
}

byte_t STAT::peek() const {
  // Most significant bit is un-mapped
  return state | 0x80;
}

byte_t STAT::read() { return peek(); }

/* This bit must be set and cleared by the pixel processor, as this register
 * does not have visibility into the values of LY and LYC to perform the updates
 * itself. */
void STAT::set_ly_eq_lyc(bool value) {
  state = (state & ~0x04);
  if (value)
    state |= 0x04;
}
const bool STAT::get_ly_eq_lyc() const { return (state & 0x04) != 0; }

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

byte_t LY::peek() const {
  assert(state >= 0 && state <= max_ly());
  return state;
}

byte_t LY::read() { return peek(); }

bool LY::inc() {
  if (state == max_ly()) {
    state = 0;
    return true;
  }
  ++state;
  return false;
}

byte_t DMGPalette::get_color_idx(byte_t idx) const {
  // Each color index in the register uses two bits
  return (state >> (idx * 2)) & 0x3;
}

}; // namespace PPU

namespace DMA {

void DMA::write(const byte_t value) {
  dma_.start(value);
  state = value;
}

} // namespace DMA

/* Always start with boot ROM mapped */
BootROMCtrl::BootROMCtrl() : MMIORegister() { map_boot_rom = true; }

/* Writing this register disables the boot ROM */
void BootROMCtrl::write(const byte_t value) {
  MMIORegister::write(value);
  map_boot_rom = false;
}

bool BootROMCtrl::boot_rom_enabled() const { return map_boot_rom; }

namespace Timer {

DIV::DIV(TimerUnit &t) : MMIORegister{}, t_(t) {}
void DIV::write(byte_t) { t_.write_div(); }
byte_t DIV::peek() const { return t_.read_div(); }
byte_t DIV::read() { return t_.read_div(); }

TIMA::TIMA(TimerUnit &t) : MMIORegister{}, t_(t) {}
void TIMA::write(byte_t v) { t_.write_tima(v); }
byte_t TIMA::peek() const { return t_.read_tima(); }
byte_t TIMA::read() { return t_.read_tima(); }

TMA::TMA(TimerUnit &t) : MMIORegister{}, t_(t) {}
void TMA::write(byte_t v) { t_.write_tma(v); }
byte_t TMA::peek() const { return t_.read_tma(); }
byte_t TMA::read() { return t_.read_tma(); }

TAC::TAC(TimerUnit &t) : MMIORegister{}, t_(t) {}
void TAC::write(byte_t v) { t_.write_tac(v); }
byte_t TAC::peek() const { return t_.read_tac(); }
byte_t TAC::read() { return t_.read_tac(); }

} // namespace Timer
