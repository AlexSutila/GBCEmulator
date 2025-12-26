#ifndef __BUS_H
#define __BUS_H

#include "cart/cart.hpp"
#include "emu_types.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

#include <array>
#include <map>
#include <memory>

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
public:
  void write_byte(const addr_t addr, const byte_t value);
  const byte_t read_byte(const addr_t addr);
  MMIORegister *get_mmio(IORegisterMapping mapping) const;
  AddressBus(Timer::TimerUnit& timer);
  void insert_cartridge(cart c);
  void eject_cartridge();
  void init_test_bed();

private:
  std::array<std::unique_ptr<byte_t[]>, 2> vram{};
  std::array<std::unique_ptr<byte_t[]>, 8> wram{};
  std::unique_ptr<byte_t[]> hram{};
  std::unique_ptr<byte_t[]> oam{};
  std::unique_ptr<Cartridge> cart_;

  /* MMIO refs maintained for convenience */
  PPU::VramBank *vram_bank_ctrl{};
  WramBank *wram_bank_ctrl{};
  BootROMCtrl *boot_rom_ctrl{};
  Timer::TimerUnit &timer_;

  /* Helpers */
  constexpr byte_t open_bus() { return 0xFF; }
  const byte_t get_vram_bank() const;
  const byte_t get_wram_bank() const;
  bool boot_rom_enabled();

  /* Maps memory mapped IO registers to their respective addresses in memory.
   * Usage of raw pointers is waranted because this map is not responsible for
   * ownership of any of the resources pointed to. */
  std::map<addr_t, std::unique_ptr<MMIORegister>> io_registers{};
  void init_io_registers();

  /* Usable hardware features are determined by the cartridge header. */
  bool is_cgb{};
};

#endif // __BUS_H
