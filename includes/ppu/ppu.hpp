#ifndef __PPU_H
#define __PPU_H

#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"

class PixelProcessor {
public:
  PixelProcessor(AddressBus *bus_ptr);
  void step();

private:
  AddressBus *const bus{};
  InterruptBits *ie_reg{};
  InterruptBits *if_reg{};
};

#endif // __PPU_H
