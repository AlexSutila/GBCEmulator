#ifndef __PPU_STATUS_H
#define __PPU_STATUS_H

#include "memory/mmio.hpp"

class LY : public MMIORegister {
  void write(byte_t value) override;
  byte_t read() override;
};

#endif // __PPU_STATUS_H
