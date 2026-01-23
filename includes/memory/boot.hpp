#ifndef __BIOS_H
#define __BIOS_H

#include "cpu/lr35902.hpp"

[[nodiscard]] LR35902::ProcessorState cgb_boot_regs();

#endif // __BIOS_H
