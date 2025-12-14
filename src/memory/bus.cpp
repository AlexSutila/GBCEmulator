#include <memory/boot.hpp>
#include <memory/bus.hpp>
#include <memory/mmio.hpp>

#include <cassert>
#include <memory>
#include <vector>

AddressBus::AddressBus() { mem = std::make_unique<byte_t[]>(0xFFFF); }

void AddressBus::init_io_registers() {
  io_registers[0xFF50] = mmio.boot_rom_ctrl.get();
}

const byte_t AddressBus::read_byte(const addr_t addr) {
  constexpr const std::vector<byte_t> &boot_rom = cgb_boot;

  /* Read from boot ROM if it is mapped */
  if (boot_rom_enabled() && addr >= 0x0000 && addr < boot_rom.size()) {
    return boot_rom.at(addr);
  }

  /* Read from memory mapped IO register */
  else if (io_registers.contains(addr)) {
    assert(addr >= 0xFF00 && addr <= 0xFF7F);
    return io_registers.at(addr)->read();
  }

  return mem[addr];
}

void AddressBus::write_byte(const addr_t addr, const byte_t value) {

  /* Write to memory mapped IO register */
  if (io_registers.contains(addr)) {
    assert(addr >= 0xFF00 && addr <= 0xFF7F);
    io_registers.at(addr)->write(value);
  }

  mem[addr] = value;
}

bool AddressBus::boot_rom_enabled() {
  return mmio.boot_rom_ctrl->boot_rom_enabled();
}
