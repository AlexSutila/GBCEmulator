#include "memory/bus.hpp"
#include "cart/cart.hpp"
#include "emu_types.hpp"
#include "gbc.hpp"
#include "memory/boot.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

/* To make the contents of this file slightly less aggregious of a playground
 * for performing heap corruption exploits lmao */
constexpr addr_t VRAM_MASK = 0x1FFF;
constexpr addr_t WRAM_MASK = 0x0FFF;
constexpr addr_t HRAM_MASK = 0x007F;

template <typename T> std::unique_ptr<T[]> make_zeroed(std::size_t size) {
  auto p = std::make_unique<T[]>(size);
  std::fill_n(p.get(), size, T{});
  return p;
}

static constexpr bool is_bootrom_range(const addr_t a) noexcept {
  return (a <= 0x00FF) || (a >= 0x0200 && a <= 0x0900);
}

static constexpr bool is_cart_range(const addr_t a) noexcept {
  return (a <= 0x7FFF) || (a >= 0xA000 && a <= 0xBFFF);
}

static constexpr bool is_vram_range(const addr_t a) noexcept {
  return (a >= 0x8000 && a <= 0x9FFF);
}

static constexpr bool is_wram_range(const addr_t a) noexcept {
  return (a >= 0xC000 && a <= 0xDFFF);
}

static constexpr bool is_echo_range(const addr_t a) noexcept {
  return (a >= 0xE000 && a <= 0xFDFF);
}

static constexpr bool is_oam_range(const addr_t a) noexcept {
  return (a >= 0xFE00 && a <= 0xFE9F);
}

static constexpr bool is_hram_range(const addr_t a) noexcept {
  return (a >= 0xFF80 && a <= 0xFFFE);
}

AddressBus::AddressBus(runtime_sys_info &sys)
    : key0(sys), // Controls backwards compatability
      key1(sys), // Controls clock speed mode
      sys_(sys)  // Generic system information
{
  constexpr std::size_t vram_bank_size = 0x2000;
  constexpr std::size_t wram_bank_size = 0x1000;
  constexpr std::size_t hram_size = 0x7F;
  constexpr std::size_t oam_size = 0xA0;
  using mmio = IORegisterMapping;

  /* Initialize banked and non-banked memory */
  std::generate(vram.begin(), vram.end(),
                [&] { return make_zeroed<byte_t>(vram_bank_size); });
  std::generate(wram.begin(), wram.end(),
                [&] { return make_zeroed<byte_t>(wram_bank_size); });
  hram = make_zeroed<byte_t>(hram_size);
  oam = make_zeroed<byte_t>(oam_size);

  /* Connect memory mapped IO owned by address bus */
  connect_mmio(static_cast<addr_t>(mmio::MMIO_BOOT_ROM_CTRL), &boot_rom_ctrl);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_WRAM_BANK), &wram_bank_ctrl);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_VRAM_BANK), &vram_bank_ctrl);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_SPD_KEY0), &key0);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_SPD_KEY1), &key1);
}

void AddressBus::connect_mmio(const addr_t addr, MMIORegister *const reg) {
  if (!reg)
    throw std::logic_error("AddressBus::connect_mmio() connected `nullptr`");
  io_registers[addr] = reg;
}

const byte_t AddressBus::get_vram_bank() const {
  if (!sys_.cgb_mode) // Unbanked for DMG
    return 0;
  return vram_bank_ctrl.get_bank();
}

const byte_t AddressBus::get_wram_bank() const {
  /* Only call for upper address range. Lower address (0xC000-0xDFFF) is always
   * mapped to bank zero, regardless of either CGB/DMG operating mode. */
  if (!sys_.cgb_mode)
    return 1;
  /* Maps to banks 1-7. Zero also maps to bank one, but that logic is handled in
   * the MMIORegister itself. This is garunteed to be between 1 and 7. */
  return wram_bank_ctrl.get_bank();
}

void AddressBus::insert_cartridge(cart c) {
  /* Generic transfer of ownership for actual game cartridges */
  cart_ = std::make_unique<Cartridge>(std::move(c));
}
void AddressBus::init_test_bed() {
  /* Default constructor initializes an instance of TestMBC */
  cart_ = std::make_unique<Cartridge>();
}
void AddressBus::eject_cartridge() { cart_.reset(); }

const byte_t AddressBus::read_byte(const addr_t addr) {
  const std::vector<byte_t> &boot_rom = get_boot_rom();

  /* Read from boot ROM if it is mapped (boot ROM overrides READs only) */
  if (boot_rom_enabled() && is_bootrom_range(addr))
    return boot_rom.at(addr);

  /* Cartridge memory */
  else if (cart_ && is_cart_range(addr))
    return cart_->read(addr);

  /* Read from VRAM, only banked in CGB mode */
  else if (is_vram_range(addr)) {
    const auto bank = get_vram_bank();
    return vram.at(bank)[(addr - 0x8000) & VRAM_MASK];
  }

  /* Read from WRAM, low bank is always mapped to zero */
  else if (is_wram_range(addr)) {
    if (addr < 0xD000)
      return wram.at(0)[(addr - 0xC000) & WRAM_MASK];
    else {
      const auto bank = get_wram_bank();
      return wram.at(bank)[(addr - 0xD000) & WRAM_MASK];
    }
  }

  /* Echoes 0xC000-0xDDFF */
  else if (is_echo_range(addr)) {
    if (addr < 0xF000)
      return wram.at(0)[(addr - 0xE000) & WRAM_MASK];
    else {
      const auto bank = get_wram_bank();
      return wram.at(bank)[(addr - 0xF000) & WRAM_MASK];
    }
  }

  /* Read from Object Attribute Memory */
  else if (is_oam_range(addr))
    return oam[addr - 0xFE00];

  /* Read from memory mapped IO register */
  else if (io_registers.contains(addr)) {
    assert((addr >= 0xFF00 && addr <= 0xFF7F) || addr == 0xFFFF);
    auto const &mmio = io_registers.at(addr);

    // Only write CGB registers if in CGB mode, fallback to 0xFF otherwise
    return (!mmio->cgb() || sys_.cgb_mode) ? mmio->read() : open_bus();
  }

  /* Read from to High RAM */
  else if (is_hram_range(addr))
    return hram[(addr - 0xFF80) & HRAM_MASK];

  /* Not actually sure what happens here, assume reads all ones */
  return open_bus();
}

void AddressBus::write_byte(const addr_t addr, const byte_t value) {

  /* Cartridge sees writes too (bank switching etc.) */
  if (cart_ && is_cart_range(addr))
    cart_->write(addr, value);

  /* Write to VRAM, only banked in CGB mode */
  else if (is_vram_range(addr)) {
    const auto bank = get_vram_bank();
    vram.at(bank)[(addr - 0x8000) & VRAM_MASK] = value;
  }

  /* Write to WRAM, low bank is always mapped to zero */
  else if (is_wram_range(addr)) {
    if (addr < 0xD000)
      wram.at(0)[(addr - 0xC000) & WRAM_MASK] = value;
    else {
      const auto bank = get_wram_bank();
      wram.at(bank)[(addr - 0xD000) & WRAM_MASK] = value;
    }
  }

  /* Echoes 0xC000-0xDDFF */
  else if (is_echo_range(addr)) {
    if (addr < 0xF000)
      wram.at(0)[(addr - 0xE000) & WRAM_MASK] = value;
    else {
      const auto bank = get_wram_bank();
      wram.at(bank)[(addr - 0xF000) & WRAM_MASK] = value;
    }
  }

  /* Write to Object Attribute Memory */
  else if (is_oam_range(addr))
    oam[addr - 0xFE00] = value;

  /* Write to memory mapped IO register */
  else if (io_registers.contains(addr)) {
    assert((addr >= 0xFF00 && addr <= 0xFF7F) || addr == 0xFFFF);
    auto const &mmio = io_registers.at(addr);

    // Only write CGB registers if in CGB mode
    if (!mmio->cgb() || sys_.cgb_mode)
      mmio->write(value);
  }

  /* Write to High RAM */
  else if (is_hram_range(addr))
    hram[(addr - 0xFF80) & HRAM_MASK] = value;
}

bool AddressBus::boot_rom_enabled() { return boot_rom_ctrl.boot_rom_enabled(); }

MMIORegister *AddressBus::get_mmio(IORegisterMapping mapping) const {
  const addr_t addr = static_cast<addr_t>(mapping);
  assert(io_registers.contains(addr));
  /* The address bus maintains ownership, so raw pointers are fine. */
  return io_registers.at(addr);
}
