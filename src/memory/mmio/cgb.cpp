#include "memory/mmio/cgb.hpp"
#include "emu_types.hpp"
#include "gbc.hpp"
#include "memory/dma.hpp"

namespace SYS {

void KEY0::write(const byte_t value) {
  /* Two is the only bit this emulator concerns itself with, though others are
   * rumored to have other purposes. */
  raw_state_set((value & dmg_mode_mask) | ~dmg_mode_mask);
  /* This will be visible to components that need to be aware about the current
   * speed mode the system is operating in. */
  sys_.cgb_mode = (raw_state() & dmg_mode_mask) == 0;
}
byte_t KEY0::peek() const {
  constexpr byte_t dmg_mode = dmg_mode_mask;
  constexpr byte_t cgb_mode = 0x00; // Bit cleared
  return sys_.cgb_mode ? cgb_mode : dmg_mode;
}
byte_t KEY0::read() { return peek(); }

void KEY1::write(const byte_t value) {
  // Speed mode is then activated by executing `STOP`
  sys_.speed_switch_armed = ((value & 0x1) != 0);
  raw_state_set(value & unused_bits_mask);
}
byte_t KEY1::peek() const {
  byte_t value = raw_state() & unused_bits_mask;
  if (sys_.speed_switch_armed)
    value = value | 0x01;
  if (sys_.double_speed)
    value = value | 0x80;
  return value;
}
byte_t KEY1::read() { return peek(); }

} // namespace SYS

namespace PPU {

void VramBank::write(const byte_t value) { raw_state_set(value | 0xFE); }
byte_t VramBank::peek() const { return raw_state() | 0xFE; }
byte_t VramBank::read() { return peek(); }

/* Only bit 0 matters, all other bits are ignored. Pandoc claims that unused
 * MMIO bits (mostly) read 1 unless specified otherwise. */
byte_t VramBank::get_bank() const { return raw_state() & 0x01; }

void PaletteIdx::write(const byte_t value) {
  // Fourth bit is unused
  raw_state_set(value | 0x40);
}
byte_t PaletteIdx::peek() const { return raw_state(); }
byte_t PaletteIdx::read() { return peek(); }

/* Writes to color RAM can increase the value stored in this register */
void PaletteIdx::inc() {
  const byte_t upper_bits = raw_state() & ~0x7F; // Careful: Include unused bit
  const byte_t addr_bits = (raw_state() + 1) & 0x3F;
  raw_state_set(upper_bits | addr_bits);
}
bool PaletteIdx::auto_inc_enabled() const { return (raw_state() & 0x80) != 0; }

/* Obtains the address used to index Color RAM */
addr_t PaletteIdx::get_address() const { return raw_state() & 0x3F; }

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
byte_t PaletteData::peek() const {
  const addr_t addr = idx_.get_address() & 0x3F;
  return mem_.at(addr);
}
byte_t PaletteData::read() { return peek(); }

/* All bits of OPRI are unused except the first bit */
void OPRI::write(const byte_t value) { raw_state_set(value | unused_mask); }
byte_t OPRI::peek() const { return raw_state() | unused_mask; }
byte_t OPRI::read() { return peek(); }

ObjectPriorityMode OPRI::get_prio_mode() const {
  byte_t prio_mode = raw_state() & ~unused_mask;
  return static_cast<ObjectPriorityMode>(prio_mode);
}

} // namespace PPU

namespace DMA {

void VDMA_ADDR::write(const byte_t value) { raw_state_set(value); }
[[nodiscard]] byte_t VDMA_ADDR::peek() const { return 0xFF; }
byte_t VDMA_ADDR::read() { return peek(); }

// Internal DMA usage only, the value read off the address bus is always FF, but
// we need to be able to see what was written to calculate source/dest addresses
// for dma transfers.
[[nodiscard]] byte_t VDMA_ADDR::get_addr_bits() const { return raw_state(); }

void VDMA_MODE_LEN::write(const byte_t value) {
  const byte_t mode_bit = (value & 0x80) >> 7;
  const auto mode = static_cast<VDMATransferMode>(mode_bit);

  constexpr byte_t size_mask = 0x7F;
  const byte_t blks = value & size_mask;

  // All logic revolving around HDMA cancel and bizarre behavior is implemented
  // within the VDMA unit itself, so calling this is completely intentional.
  dma_.enable(mode, blks);
}

byte_t VDMA_MODE_LEN::peek() const {
  constexpr byte_t complete_mask = 0x80;
  constexpr byte_t size_mask = 0x7F;
  if (dma_.complete())
    return complete_mask | dma_.get_blks_remaining();

  // If the DMA is still in progress, it just shows the size. The seventh
  // bit indicates that the full data transfer is complete.
  return dma_.get_blks_remaining() & size_mask;
}
byte_t VDMA_MODE_LEN::read() { return peek(); }

} // namespace DMA

void WramBank::write(const byte_t value) { raw_state_set(value | 0xF8); }
byte_t WramBank::peek() const { return raw_state() | 0xF8; }
byte_t WramBank::read() { return peek(); }

/* Only bits 0-2 matter, and only values 1-7 actually map to their respective
 * banks. If zero is written, it will map to bank 1, as bank 0 can always be
 * used from the 0xC000-0xCFFF address range. */
byte_t WramBank::get_bank() const {
  byte_t ret = raw_state() & 0x07; // Only read bits 0-2
  if (ret == 0)
    ++ret;
  return ret;
}
