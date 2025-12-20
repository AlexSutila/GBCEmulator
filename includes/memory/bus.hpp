#ifndef __BUS_H
#define __BUS_H

#include "emu_types.hpp"
#include "memory/boot.hpp"
#include "memory/mmio.hpp"

#include <map>
#include <memory>

#include "cart/cart.hpp"

/*
 * Game Boy Memory Map
 *
 *  Start   End     Description
 *  ---------------------------------------------------
 *  0000    3FFF    16 KiB ROM Bank 00
 *  4000    7FFF    16 KiB ROM Bank 01–NN
 *  8000    9FFF    8 KiB Video RAM (VRAM)
 *  A000    BFFF    8 KiB External RAM
 *  C000    CFFF    4 KiB Work RAM (WRAM)
 *  D000    DFFF    4 KiB Work RAM (WRAM)
 *  E000    FDFF    Echo RAM (mirror of C000–DDFF)
 *  FE00    FE9F    Object Attribute Memory (OAM)
 *  FEA0    FEFF    Not Usable
 *  FF00    FF7F    I/O Registers
 *  FF80    FFFE    High RAM (HRAM)
 *  FFFF    FFFF    Interrupt Enable Register (IE)
 */

class AddressBus {
  std::unique_ptr<Cartridge> cart_;

public:
  void write_byte(const addr_t addr, const byte_t value);
  const byte_t read_byte(const addr_t addr);
  MMIORegister *get_mmio(IORegisterMapping mapping) const;
  AddressBus();
  void insert_cartridge(cart c) {
    cart_ = std::make_unique<Cartridge>(std::move(c));
  }
  void eject_cartridge() { cart_.reset(); }

private:
  std::unique_ptr<byte_t[]> mem{};
  void init_io_registers();

  /* Maintain a pointer to the boot rom control register for convenience. */
  BootROMCtrl *boot_rom_ctrl{};
  bool boot_rom_enabled();

  /* Maps memory mapped IO registers to their respective addresses in memory.
   * Usage of raw pointers is waranted because this map is not responsible for
   * ownership of any of the resources pointed to. */
  std::map<addr_t, std::unique_ptr<MMIORegister>> io_registers{};
};

#endif // __BUS_H
