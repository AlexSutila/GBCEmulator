#include "memory/mmio/dmg.hpp"
#include "cpu/interrupts.hpp"
#include "emu_types.hpp"
#include "savestate/codec.hpp"
#include "timer.hpp"
#include <cassert>

namespace Serial {

SerialCtrl::SerialCtrl(MMIORegister &serial_data) : sd(serial_data) {}

/* We completely ignore clock speed because we just straight up assume that the
 * transfer completes instantly, then we don't actually transfer anything. */
void SerialCtrl::write(const byte_t value) {
  if ((value & 0x81) == 0x81) {    // Use internal clk and transfer in progress
    state_ = (value & 0x3) | 0x7C; // Clear transfer in progress bit
    sd.write(0xFF);                // No connection, so read ones
    // This is hacky and inaccurate, just shoot the interrupt out instantly
    if_reg->put_flag(InterruptFlagMask::INT_FLAG_SERIAL, true);
  }
}
void SerialCtrl::set_interrupt_reg(InterruptBits *reg) { if_reg = reg; }
byte_t SerialCtrl::peek() const { return state_ | 0x7C; }
byte_t SerialCtrl::read() { return peek(); }

} // namespace Serial

namespace PPU {

/* LCD Control helpers */
bool LCDCtrl::lcd_enabled() const { return (state_ & 0x80) != 0; }
TileMapArea LCDCtrl::win_tilemap_base() const {
  return (state_ & 0x40) != 0 ? TileMapArea::HI_TILEMAP_BASE : TileMapArea::LO_TILEMAP_BASE;
}

bool LCDCtrl::win_enabled() const { return (state_ & 0x20) != 0; }
TileDataArea LCDCtrl::bg_win_data_area() const {
  return (state_ & 0x10) != 0 ? TileDataArea::HI_TILEDATA_BASE : TileDataArea::LO_TILEDATA_BASE;
}
TileMapArea LCDCtrl::bg_tilemap_base() const {
  return (state_ & 0x08) != 0 ? TileMapArea::HI_TILEMAP_BASE : TileMapArea::LO_TILEMAP_BASE;
}
bool LCDCtrl::bg_win_en_priority() const { return (state_ & 0x1) != 0; }

/* The return value here will always be in reference to the height (pixels) of
 * the sprites. Sprites will never not be 8 pixels wide. */
SpriteHeight LCDCtrl::obj_size() const {
  return (state_ & 0x04) != 0 ? SpriteHeight::TALL_SPRITES : SpriteHeight::SHORT_SPRITES;
}
bool LCDCtrl::obj_enable() const { return (state_ & 0x02) != 0; }

void STAT::write(const byte_t value) {
  // Most significant bit is un-mapped, preserve mode bits
  state_ = ((state_ & 0x03) | (value & 0x7C) | 0x80);
}

byte_t STAT::peek() const {
  // Most significant bit is un-mapped
  return state_ | 0x80;
}

byte_t STAT::read() { return peek(); }

/* This bit must be set and cleared by the pixel processor, as this register
 * does not have visibility into the values of LY and LYC to perform the updates
 * itself. */
void STAT::set_ly_eq_lyc(bool value) {
  state_ = (state_ & ~0x04);
  if (value)
    state_ |= 0x04;
}
bool STAT::get_ly_eq_lyc() const { return (state_ & 0x04) != 0; }

StatModes STAT::get_mode() const {
  constexpr byte_t mode_mask = 0x03;
  const auto mode = static_cast<StatModes>(state_ & mode_mask);
  return mode;
}

void STAT::set_mode(StatModes mode) {
  constexpr byte_t mode_mask = 0x03;
  const auto mode_bits = static_cast<byte_t>(mode);
  state_ = state_ & ~mode_mask;
  state_ |= mode_bits;
}

void LY::write(byte_t) { /* Read only */ }

byte_t LY::peek() const {
  assert(state_ >= 0 && state_ <= max_ly());
  return state_;
}

byte_t LY::read() { return peek(); }

bool LY::inc() {
  if (state_ == max_ly()) {
    state_ = 0;
    return true;
  }
  ++state_;
  return false;
}

byte_t DMGPalette::get_color_idx(const byte_t idx) const {
  // Each color index in the register uses two bits
  return (state_ >> (idx * 2)) & 0x3;
}

}; // namespace PPU

namespace DMA {

void DMA::write(const byte_t value) {
  dma_.start(value);
  state_ = value;
}

} // namespace DMA

/* Always start with boot ROM mapped */
BootROMCtrl::BootROMCtrl() : MMIORegister() { map_boot_rom = true; }

template <typename T> void BootROMCtrl::parse_savestate(T &t) {
  t.field_generic(1, map_boot_rom); // Not enum worthy
}

template void BootROMCtrl::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void BootROMCtrl::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void BootROMCtrl::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void BootROMCtrl::parse_savestate<Savestate::Checker>(Savestate::Checker &);

/* Writing this register disables the boot ROM */
void BootROMCtrl::write(const byte_t value) {
  MMIORegister::write(value);
  map_boot_rom = false;
}

bool BootROMCtrl::boot_rom_enabled() const { return map_boot_rom; }

void BootROMCtrl::set_boot_rom_enabled(const bool enabled) { map_boot_rom = enabled; }

namespace Timer {

DIV::DIV(TimerUnit &t) : t_(t) {}
void DIV::write(byte_t) { t_.write_div(); }
byte_t DIV::peek() const { return t_.read_div(); }
byte_t DIV::read() { return t_.read_div(); }

TIMA::TIMA(TimerUnit &t) : t_(t) {}
void TIMA::write(const byte_t v) { t_.write_tima(v); }
byte_t TIMA::peek() const { return t_.read_tima(); }
byte_t TIMA::read() { return t_.read_tima(); }

TMA::TMA(TimerUnit &t) : t_(t) {}
void TMA::write(const byte_t v) { t_.write_tma(v); }
byte_t TMA::peek() const { return t_.read_tma(); }
byte_t TMA::read() { return t_.read_tma(); }

TAC::TAC(TimerUnit &t) : t_(t) {}
void TAC::write(const byte_t v) { t_.write_tac(v); }
byte_t TAC::peek() const { return t_.read_tac(); }
byte_t TAC::read() { return t_.read_tac(); }

} // namespace Timer
