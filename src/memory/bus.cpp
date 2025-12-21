#include "memory/bus.hpp"
#include "cpu/interrupts.hpp"
#include "emu_types.hpp"
#include "memory/boot.hpp"
#include "memory/mmio.hpp"
#include "ppu/status.hpp"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <vector>

AddressBus::AddressBus() {
  mem = std::make_unique<byte_t[]>(0x10000);
  init_io_registers();

  /* Maintain this for convenience during memory access */
  auto *reg = get_mmio(IORegisterMapping::MMIO_BOOT_ROM_CTRL);
  if (!(boot_rom_ctrl = dynamic_cast<BootROMCtrl *>(reg)))
    throw std::logic_error("Failed to connect MMIO_BOOT_ROM_CTRL");
}

void AddressBus::init_io_registers() {
  io_registers[0xFF0F] = std::make_unique<InterruptBits>(true);
  io_registers[0xFF44] = std::make_unique<LY>();
  io_registers[0xFF50] = std::make_unique<BootROMCtrl>();
  io_registers[0xFFFF] = std::make_unique<InterruptBits>(false);
}

static constexpr bool is_cart_range(const addr_t a) noexcept {
  return (a <= 0x7FFF) || (a >= 0xA000 && a <= 0xBFFF);
}

static constexpr bool is_bootrom_range(const addr_t a) noexcept {
  return (a <= 0x00FF) || (a >= 0x0200 && a <= 0x0900);
}

const byte_t AddressBus::read_byte(const addr_t addr) {
  const std::vector<byte_t> &boot_rom = get_boot_rom();

  /* Read from boot ROM if it is mapped (boot ROM overrides READs only) */
  if (boot_rom_enabled() && is_bootrom_range(addr)) {
    return boot_rom.at(addr);
  }

  /* Cartridge memory */
  if (cart_ && is_cart_range(addr)) {
    return cart_->read(addr);
  }

  /* Read from memory mapped IO register */
  if (io_registers.contains(addr)) {
    assert((addr >= 0xFF00 && addr <= 0xFF7F) || addr == 0xFFFF);
    return io_registers.at(addr)->read();
  }

  /* Fallback memory */
  return mem[addr];
}

void AddressBus::write_byte(const addr_t addr, const byte_t value) {

  /* Cartridge sees writes too (bank switching etc.) */
  if (cart_ && is_cart_range(addr)) {
    cart_->write(addr, value);
    return;
  }

  /* Write to memory mapped IO register */
  if (io_registers.contains(addr)) {
    assert((addr >= 0xFF00 && addr <= 0xFF7F) || addr == 0xFFFF);
    io_registers.at(addr)->write(value);
  }

  /* Fallback memory */
  else
    mem[addr] = value;
}

bool AddressBus::boot_rom_enabled() {
  return boot_rom_ctrl->boot_rom_enabled();
}

MMIORegister *AddressBus::get_mmio(IORegisterMapping mapping) const {
  const addr_t addr = static_cast<addr_t>(mapping);
  assert(io_registers.contains(addr));
  /* The address bus maintains ownership, so raw pointers are fine. */
  return io_registers.at(addr).get();
}
