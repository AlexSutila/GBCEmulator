#ifndef __VRAM_H
#define __VRAM_H

#include "emu_types.hpp"
#include "memory/mmio.hpp"

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

#endif // __VRAM_H
