#ifndef __MMIO_CGB_H
#define __MMIO_CGB_H

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"

namespace PPU {

/*
 * FF4F - VBK: VRAM Bank
 *
 * This register can be written to change VRAM banks. Only bit 0 matters, all
 * other bits are ignored.
 */
class VramBank : public MMIORegister {
public:
  void write(const byte_t value);
  byte_t read();
  VramBank() : MMIORegister(0), state(0) {}

  constexpr bool cgb() { return true; }
  const byte_t get_bank() const;

private:
  byte_t state{};
};

} // namespace PPU

/*
 * FF70 - SVBK/WBK: WRAM Bank
 *
 * In CGB Mode, 32 KiB of internal RAM are available. This memory is divided
 * into 8 banks of 4 KiB each. Bank 0 is always available in memory at
 * C000–CFFF, banks 1–7 can be selected into the address space at D000–DFFF.
 */
class WramBank : public MMIORegister {
public:
  void write(const byte_t value);
  byte_t read();
  WramBank() : MMIORegister(0), state(1) {}

  constexpr bool cgb() { return true; }
  const byte_t get_bank() const;

private:
  byte_t state{};
};

#endif // __MMIO_CGB_H
