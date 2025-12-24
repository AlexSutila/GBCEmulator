#include "memory/bus.hpp"
#include "cart/cart.hpp"
#include "cpu/interrupts.hpp"
#include "emu_types.hpp"
#include "memory/boot.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

AddressBus::AddressBus() {
  constexpr std::size_t vram_bank_size = 0x2000;
  constexpr std::size_t wram_bank_size = 0x1000;
  using ioregs = IORegisterMapping;

  /* Initialize VRAM, two banks in CGB mode, second bank unused for DMG */
  for (auto &bank : vram) {
    bank = std::make_unique<byte_t[]>(vram_bank_size);
    std::fill_n(bank.get(), vram_bank_size, 0);
  }

  /* Initialize WRAM, eight banks in CGB mode, two for DMG */
  for (auto &bank : wram) {
    bank = std::make_unique<byte_t[]>(wram_bank_size);
    std::fill_n(bank.get(), wram_bank_size, 0);
  }

  /* Populates io-registers lookup table */
  init_io_registers();

  /* We maintain access to various mmio registers via a raw pointer
   * for access convenience during memory reads and writes. */
  auto *reg = get_mmio(ioregs::MMIO_BOOT_ROM_CTRL);
  if (!(boot_rom_ctrl = dynamic_cast<BootROMCtrl *>(reg)))
    throw std::logic_error("Failed to connect MMIO_BOOT_ROM_CTRL");
  reg = get_mmio(ioregs::MMIO_VRAM_BANK);
  if (!(vram_bank_ctrl = dynamic_cast<PPU::VramBank *>(reg)))
    throw std::logic_error("Failed to connect MMIO_VRAM_BANK");
  reg = get_mmio(ioregs::MMIO_WRAM_BANK);
  if (!(wram_bank_ctrl = dynamic_cast<WramBank *>(reg)))
    throw std::logic_error("Failed to connect MMIO_WRAM_BANK");

  /* TODO: Remove fallback memory */
  mem = std::make_unique<byte_t[]>(0x10000);
}

void AddressBus::init_io_registers() {
  io_registers[0xFF0F] = std::make_unique<::InterruptBits>(true);
  io_registers[0xFF41] = std::make_unique<PPU::STAT>();
  io_registers[0xFF44] = std::make_unique<PPU::LY>();
  io_registers[0xFF45] = std::make_unique<::MMIORegister>(); // LYC
  io_registers[0xFF4F] = std::make_unique<PPU::VramBank>();
  io_registers[0xFF50] = std::make_unique<::BootROMCtrl>();
  io_registers[0xFF70] = std::make_unique<::WramBank>();
  io_registers[0xFFFF] = std::make_unique<::InterruptBits>(false);
}

const byte_t AddressBus::get_vram_bank() const {
  if (!is_cgb) // Unbanked for DMG
    return 0;
  return vram_bank_ctrl->get_bank();
}

const byte_t AddressBus::get_wram_bank() const {
  /* Only call for upper address range. Lower address (0xC000-0xDFFF) is always
   * mapped to bank zero, regardless of either CGB/DMG operating mode. */
  if (!is_cgb)
    return 1;
  /* Maps to banks 1-7. Zero also maps to bank one, but that logic is handled in
   * the MMIORegister itself. This is garunteed to be between 1 and 7. */
  return wram_bank_ctrl->get_bank();
}

void AddressBus::insert_cartridge(cart c) {
  cart_ = std::make_unique<Cartridge>(std::move(c));
  const byte_t cgb_flag = c.header.cgb_flag();

  /* May limit interaction with specific MMIO if disabled */
  is_cgb = cgb_enabled(cgb_flag);
}
void AddressBus::eject_cartridge() { cart_.reset(); }

static constexpr bool is_bootrom_range(const addr_t a) noexcept {
  return (a <= 0x00FF) || (a >= 0x0200 && a <= 0x0900);
}

static constexpr bool is_cart_range(const addr_t a) noexcept {
  return (a <= 0x7FFF) || (a >= 0xA000 && a <= 0xBFFF);
}

static constexpr bool is_vram_range(const addr_t a) noexcept {
  return (a >= 0x8000 && a <= 0x9FFF);
}

static constexpr bool is_wram_range(const addr_t a, bool high) noexcept {
  return high ? (a >= 0xD000 && a <= 0xDFFF) : (a >= 0xC000 & a <= 0xCFFF);
}

const byte_t AddressBus::read_byte(const addr_t addr) {
  const std::vector<byte_t> &boot_rom = get_boot_rom();

  /* Read from boot ROM if it is mapped (boot ROM overrides READs only) */
  if (boot_rom_enabled() && is_bootrom_range(addr)) {
    return boot_rom.at(addr);
  }

  /* Cartridge memory */
  else if (cart_ && is_cart_range(addr)) {
    return cart_->read(addr);
  }

  /* Read from VRAM, only banked in CGB mode */
  else if (is_vram_range(addr)) {
    const auto bank = get_vram_bank();
    return vram.at(bank)[addr - 0x8000];
  }

  /* Read from WRAM, low bank is always mapped to zero */
  else if (is_wram_range(addr, false)) {
    return wram.at(0)[addr - 0xC000];
  }

  /* Read from WRAM, high bank mapped 1-7 for CGB */
  else if (is_wram_range(addr, true)) {
    const auto bank = get_wram_bank();
    return wram.at(bank)[addr - 0xD000];
  }

  /* Read from memory mapped IO register */
  else if (io_registers.contains(addr)) {
    assert((addr >= 0xFF00 && addr <= 0xFF7F) || addr == 0xFFFF);
    auto const &mmio = io_registers.at(addr);

    // Only write CGB registers if in CGB mode, fallback to 0xFF otherwise
    return (!mmio->cgb() || is_cgb) ? mmio->read() : 0xFF;
  }

  /* Fallback memory */
  else
    return mem[addr];
}

void AddressBus::write_byte(const addr_t addr, const byte_t value) {

  /* Cartridge sees writes too (bank switching etc.) */
  if (cart_ && is_cart_range(addr)) {
    cart_->write(addr, value);
  }

  /* Write to VRAM, only banked in CGB mode */
  else if (is_vram_range(addr)) {
    const auto bank = get_vram_bank();
    vram.at(bank)[addr - 0x8000] = value;
  }

  /* Write to WRAM, low bank is always mapped to zero */
  else if (is_wram_range(addr, false)) {
    wram.at(0)[addr - 0xC000] = value;
  }

  /* Write to WRAM, high bank mapped 1-7 for CGB */
  else if (is_wram_range(addr, true)) {
    const auto bank = get_wram_bank();
    wram.at(bank)[addr - 0xD000] = value;
  }

  /* Write to memory mapped IO register */
  else if (io_registers.contains(addr)) {
    assert((addr >= 0xFF00 && addr <= 0xFF7F) || addr == 0xFFFF);
    auto const &mmio = io_registers.at(addr);

    // Only write CGB registers if in CGB mode
    if (!mmio->cgb() || is_cgb)
      mmio->write(value);
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
