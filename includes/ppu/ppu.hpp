#ifndef __PPU_H
#define __PPU_H

#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"
#include "memory/mmio.hpp"
#include "ppu/status.hpp"

class PixelProcessor {
public:
  PixelProcessor(AddressBus *bus_ptr);
  void step();

private:
  AddressBus *const bus{};
  InterruptBits *ie_reg{};
  InterruptBits *if_reg{};

  /* Pixel Processor status registers */
  PPU::STAT *stat_reg{};
  PPU::LY *ly_reg{};
  MMIORegister *lyc_reg{};
};

#endif // __PPU_H
