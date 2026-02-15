#ifndef __BUS_H
#define __BUS_H

#include "cart/cart.hpp"
#include "debugger/debugger.hpp"
#include "emu_types.hpp"
#include "memory/dma.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

struct runtime_sys_info;
class BootROM;

enum BusConflictTypes : std::uint32_t {
  BUS_CONFLICT_NONE = 0,
  BUS_CONFLICT_OAM_DMA = 1 << 1,
};

constexpr BusConflictTypes operator|(BusConflictTypes a, BusConflictTypes b) {
  return static_cast<BusConflictTypes>(static_cast<std::uint32_t>(a) |
                                       static_cast<std::uint32_t>(b));
}

constexpr BusConflictTypes operator&(BusConflictTypes a, BusConflictTypes b) {
  return static_cast<BusConflictTypes>(static_cast<std::uint32_t>(a) &
                                       static_cast<std::uint32_t>(b));
}

constexpr BusConflictTypes operator~(BusConflictTypes a) {
  return static_cast<BusConflictTypes>(~static_cast<std::uint32_t>(a));
}

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
class AddressBus final : private Debug::Debuggable {
public:
  void write_byte(const addr_t addr, const byte_t value);
  const byte_t read_byte(const addr_t addr);
  ObjAttrDMA &get_oam_dma() { return oam_dma; };
  VDMA &get_vdma() { return vdma; }

  /* To be used by debuggers, more or less reads memory exactly the same as the
   * regular `read_byte()`, but calls `peak()` for memory mapped registers. */
  const byte_t read_byte_safe(const addr_t addr);

  /* Second constructor is called when skipping BIOS, first constructor may also
   * ignore the BIOS if the initialization fails for some reason. */
  AddressBus(runtime_sys_info &sys, std::optional<Debug::Debugger> &debugger,
             std::optional<BootROM> &bios);

  /* For attaching MMIO component interface registers */
  void connect_mmio(const addr_t addr, MMIORegister *const reg);
  MMIORegister *get_mmio(IORegisterMapping mapping) const;

  /* Bus conflict management */
  bool is_acquired(BusConflictTypes conflict_mask) const;
  void acquire(BusConflictTypes conflict_mask);
  void release(BusConflictTypes conflict_mask);

  /* Cartridge connections */
  void insert_cartridge(cart c);
  void eject_cartridge();
  void init_test_bed();

  /* Convenience getters for PixelProcessor */
  std::array<std::unique_ptr<byte_t[]>, 2> &get_vram() { return vram; }
  std::unique_ptr<byte_t[]> &get_oam() { return oam; }

private:
  std::array<std::unique_ptr<byte_t[]>, 2> vram{};
  std::array<std::unique_ptr<byte_t[]>, 8> wram{};
  std::unique_ptr<byte_t[]> hram{};
  std::unique_ptr<byte_t[]> oam{};
  std::unique_ptr<Cartridge> cart_;
  Joypad::JOYP joypad_;

  /* System control registers: (speed mode, backwards compatability, etc) */
  SYS::KEY0 key0; // Controls DMG backwards compatability
  SYS::KEY1 key1; // Controls clock speed mode

  /* Direct memory access routine modules */
  ObjAttrDMA oam_dma;
  VDMA vdma;

  /* MMIO refs maintained for convenience */
  PPU::VramBank vram_bank_ctrl{};
  WramBank wram_bank_ctrl{};
  BootROMCtrl boot_rom_ctrl{};

  /* Denotes who is currently holding onto what address ranges. In the case
   * of bus conflicts, one component will end up reading what we are basically
   * going to be treating as `open bus`. */
  constexpr byte_t open_bus() { return 0xFF; }
  bool is_conflicting(const addr_t addr) const;
  BusConflictTypes bus_conflicts{};

  std::map<addr_t, MMIORegister *> io_registers{};
  bool is_boot_rom_range(const addr_t a);
  std::optional<BootROM> &bios_;
  runtime_sys_info &sys_;
};

#endif // __BUS_H
