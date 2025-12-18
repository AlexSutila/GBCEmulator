#include "memory/boot.hpp"
#include "emu_types.hpp"

/* Always start with boot ROM mapped */
BootROMCtrl::BootROMCtrl() : MMIORegister() { map_boot_rom = true; }

/* Writing this register disables the boot ROM */
void BootROMCtrl::write(const byte_t value) {
  MMIORegister::write(value);
  map_boot_rom = false;
}

byte_t BootROMCtrl::read() { return MMIORegister::read(); }

bool BootROMCtrl::boot_rom_enabled() const { return map_boot_rom; }
