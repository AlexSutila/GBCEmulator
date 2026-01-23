#include "memory/boot.hpp"

LR35902::ProcessorState cgb_boot_regs() {
  LR35902::ProcessorState boot_regs = {
      .pc = 0x0100,
      .sp = 0xFFFE,
      .a = 0x11,
      .b = 0x00,
      .c = 0x14,
      .d = 0x00,
      .e = 0x00,
      .f = 0x00,
      .h = 0xC0,
      .l = 0x60,
  };
  return boot_regs;
}
