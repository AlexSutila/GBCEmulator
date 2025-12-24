#ifndef __MMIO_CGB_H
#define __MMIO_CGB_H

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"

namespace PPU {

class VramBank : public MMIORegister {
public:
  void write(const byte_t value);
  byte_t read();
  VramBank() : MMIORegister(0) {}

  /* Only usable by CGB, as DMG does not bank VRAM */
  constexpr bool cgb() { return true; }
  const byte_t get_bank() const;

private:
  byte_t state;
};

} // namespace PPU

#endif // __MMIO_CGB_H
