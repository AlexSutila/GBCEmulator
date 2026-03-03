#include "memory/bus.hpp"
#include "cart/cart.hpp"
#include "debugger/breakpoint.hpp"
#include "emu_types.hpp"
#include "gbc.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "savestate/codec.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>

/* To make the contents of this file slightly less egregious of a playground
 * for performing heap corruption exploits lmao */
constexpr addr_t VRAM_MASK = 0x1FFF;
constexpr addr_t WRAM_MASK = 0x0FFF;
constexpr addr_t HRAM_MASK = 0x007F;

template <typename T> std::unique_ptr<T[]> make_zeroed(std::size_t size) {
  auto p = std::make_unique<T[]>(size);
  std::fill_n(p.get(), size, T{});
  return p;
}

bool AddressBus::is_boot_rom_range(const addr_t a) const {
  if (!boot_rom_ctrl.boot_rom_enabled() || !bios_.has_value())
    return false;
  return bios_->in_range(a);
}

static constexpr bool is_cart_range(const addr_t a) noexcept {
  return a <= 0x7FFF || (a >= 0xA000 && a <= 0xBFFF);
}

static constexpr bool is_vram_range(const addr_t a) noexcept {
  return a >= 0x8000 && a <= 0x9FFF;
}

static constexpr bool is_wram_range(const addr_t a) noexcept {
  return a >= 0xC000 && a <= 0xDFFF;
}

static constexpr bool is_echo_range(const addr_t a) noexcept {
  return a >= 0xE000 && a <= 0xFDFF;
}

static constexpr bool is_oam_range(const addr_t a) noexcept {
  return a >= 0xFE00 && a <= 0xFE9F;
}

static constexpr bool is_hram_range(const addr_t a) noexcept {
  return a >= 0xFF80 && a <= 0xFFFE;
}

AddressBus::AddressBus(runtime_sys_info &sys,
                       std::optional<Debug::Debugger> &debugger,
                       std::optional<BootROM> &bios)
    : Debuggable(debugger), // Bus read/write breakpoints
      key0(sys),            // Controls backwards compatability
      key1(sys),            // Controls clock speed mode
      oam_dma(*this),       // Performs object attribute DMA (DMG/CGB)
      vdma(*this, sys),     // Performs GDMA and HDMA (CGB only)
      bios_(bios),          // Optionally configured by frontend
      sys_(sys)             // Generic system information
{
  constexpr std::size_t vram_bank_size = 0x2000;
  constexpr std::size_t wram_bank_size = 0x1000;
  constexpr std::size_t hram_size = 0x7F;
  constexpr std::size_t oam_size = 0xA0;
  using mmio = IORegisterMapping;
  using namespace std::ranges;

  /* Initialize banked and non-banked memory */
  generate(vram, [&] { return make_zeroed<byte_t>(vram_bank_size); });
  generate(wram, [&] { return make_zeroed<byte_t>(wram_bank_size); });
  hram = make_zeroed<byte_t>(hram_size);
  oam = make_zeroed<byte_t>(oam_size);
  bus_conflicts = BUS_CONFLICT_NONE;

  /* Connect memory mapped IO owned by address bus */
  connect_mmio(static_cast<addr_t>(mmio::MMIO_JOYPAD), &joypad_,
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_BOOT_ROM_CTRL), &boot_rom_ctrl,
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_WRAM_BANK), &wram_bank_ctrl,
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_VRAM_BANK), &vram_bank_ctrl,
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_SPD_KEY0), &key0,
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_SPD_KEY1), &key1,
               MMIOSavestatePolicy::BusAuto);

  /* Connect memory mapped IO owned by DMA modules */
  connect_mmio(static_cast<addr_t>(mmio::MMIO_OAM_DMA), oam_dma.get_dma_reg(),
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA1), vdma.get_vdma1(),
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA2), vdma.get_vdma2(),
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA3), vdma.get_vdma3(),
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA4), vdma.get_vdma4(),
               MMIOSavestatePolicy::BusAuto);
  connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA5), vdma.get_vdma5());
}

void AddressBus::connect_mmio(const addr_t addr, MMIORegister *const reg,
                              const MMIOSavestatePolicy policy) {
  if (!reg)
    throw std::logic_error("AddressBus::connect_mmio() connected `nullptr`");
  io_registers[addr] = ConnectedMMIO{
      .reg = reg,
      .savestate_policy = policy,
  };
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

byte_t &AddressBus::vram_byte(const addr_t addr) const {
  const auto bank = vram_bank_ctrl.get_bank();
  return vram.at(bank)[(addr - 0x8000) & VRAM_MASK];
}

byte_t &AddressBus::wram_byte(const addr_t addr) const {
  if (addr < 0xD000) // Only one half is banked
    return wram.at(0)[(addr - 0xC000) & WRAM_MASK];
  const auto bank = wram_bank_ctrl.get_bank();
  return wram.at(bank)[(addr - 0xD000) & WRAM_MASK];
}

byte_t &AddressBus::echo_byte(const addr_t addr) const {
  if (addr < 0xF000)
    return wram.at(0)[(addr - 0xE000) & WRAM_MASK];
  const auto bank = wram_bank_ctrl.get_bank();
  return wram.at(bank)[(addr - 0xF000) & WRAM_MASK];
}

byte_t &AddressBus::oam_byte(const addr_t addr) const {
  return oam[addr - 0xFE00];
}

byte_t &AddressBus::hram_byte(const addr_t addr) const {
  return hram[(addr - 0xFF80) & HRAM_MASK];
}

byte_t AddressBus::read_byte_safe(const addr_t addr) const {
  if (io_registers.contains(addr)) {
    assert((addr >= 0xFF00 && addr <= 0xFF7F) || addr == 0xFFFF);
    const auto & [reg, savestate_policy] = io_registers.at(addr);
    return reg->peek(); // Const
  }
  return read_byte(addr, false);
}

byte_t AddressBus::read_byte(const addr_t addr, bool debug) const {
  try_brk(addr, Debug::BRK_ADDRESS_READ);
  if (is_conflicting(addr)) [[unlikely]]
    return open_bus();
  if (has_cheat_overrides_ && cheat_mask_[addr]) [[unlikely]]
    return cheat_values_[addr];

  /* Read from boot ROM if it is mapped (boot ROM overrides reads only) */
  if (is_boot_rom_range(addr))
    return bios_->read_byte(addr);

  /* Cartridge memory */
  if (cart_ && is_cart_range(addr))
    return cart_->read_byte(addr);

  /* Read from VRAM, only banked in CGB mode */
  if (is_vram_range(addr))
    return vram_byte(addr);

  /* Read from WRAM, low bank is always mapped to zero */
  if (is_wram_range(addr))
    return wram_byte(addr);

  /* Echoes 0xC000-0xDDFF */
  if (is_echo_range(addr))
    return echo_byte(addr);

  /* Read from Object Attribute Memory */
  if (is_oam_range(addr))
    return oam_byte(addr);

  /* Read from memory mapped IO register */
  if (io_registers.contains(addr)) {
    assert((addr >= 0xFF00 && addr <= 0xFF7F) || addr == 0xFFFF);
    const auto & [reg, savestate_policy] = io_registers.at(addr);
    return reg->read();
  }

  /* Read from to High RAM */
  if (is_hram_range(addr))
    return hram_byte(addr);

  return open_bus();
}

void AddressBus::write_byte(const addr_t addr, const byte_t value) const {
  if (is_conflicting(addr)) [[unlikely]]
    return;

  /* Cartridge sees writes too (bank switching etc.) */
  if (cart_ && is_cart_range(addr))
    cart_->write(addr, value);

  /* Write to VRAM, only banked in CGB mode */
  else if (is_vram_range(addr))
    vram_byte(addr) = value;

  /* Write to WRAM, low bank is always mapped to zero */
  else if (is_wram_range(addr))
    wram_byte(addr) = value;

  /* Echoes 0xC000-0xDDFF */
  else if (is_echo_range(addr))
    echo_byte(addr) = value;

  /* Write to Object Attribute Memory */
  else if (is_oam_range(addr))
    oam_byte(addr) = value;

  /* Write to memory mapped IO register */
  else if (io_registers.contains(addr)) {
    assert((addr >= 0xFF00 && addr <= 0xFF7F) || addr == 0xFFFF);
    auto const &mmio = io_registers.at(addr);
    mmio.reg->write(value);
  }

  /* Write to High RAM */
  else if (is_hram_range(addr))
    hram_byte(addr) = value;

  /* We intentionally evaluate the breakpoint after the value has been written,
   * as it is less confusing from a UI perspective, seeing the updated value. */
  try_brk(addr, Debug::BRK_ADDRESS_WRITTEN);
}

MMIORegister *AddressBus::get_mmio(IORegisterMapping mapping) const {
  const auto addr = static_cast<addr_t>(mapping);
  assert(io_registers.contains(addr));
  /* The address bus maintains ownership, so raw pointers are fine. */
  return io_registers.at(addr).reg;
}

/**
 * TODO (tentative):
 * - We might want to consider VRAM locking during certain PPU modes, though
 *   it probably isn't a good idea to mess with this until we are certain our
 *   PPU timings are perfect.
 * - Although I could imagine VDMA might cause problems, I don't think it is
 *   worth simulating its impacts on the address bus since the processor has its
 *   execution suspended while VDMA is active, so eh...
 */
bool AddressBus::is_conflicting(const addr_t addr) const {

  /* OAM DMA transfer is active, hence OAM is locked down */
  if ((bus_conflicts & BUS_CONFLICT_OAM_DMA) != 0)
    return is_oam_range(addr); // TODO: Is this all?

  /* Anything is fair game, read/write freely */
  return false;
}

bool AddressBus::is_acquired(const BusConflictTypes conflict_mask) const {
  return (bus_conflicts & conflict_mask) != 0;
}

void AddressBus::acquire(const BusConflictTypes conflict_mask) {
  bus_conflicts = bus_conflicts | conflict_mask;
}

void AddressBus::release(const BusConflictTypes conflict_mask) {
  bus_conflicts = bus_conflicts & ~conflict_mask;
}

void AddressBus::clear_cheat_overrides() {
  for (const addr_t addr : cheat_touched_addrs_)
    cheat_mask_[addr] = false;
  cheat_touched_addrs_.clear();
  has_cheat_overrides_ = false;
}

void AddressBus::set_cheat_overrides(std::span<const CheatOverride> overrides) {
  clear_cheat_overrides();
  cheat_touched_addrs_.reserve(overrides.size());

  for (const auto & [addr, value] : overrides) {
    const auto idx = static_cast<std::size_t>(addr);
    if (!cheat_mask_[idx])
      cheat_touched_addrs_.push_back(addr);
    cheat_mask_[idx] = true;
    cheat_values_[idx] = value;
  }
  has_cheat_overrides_ = !cheat_touched_addrs_.empty();
}

enum : std::uint16_t {
  F_VRAM = 1,
  F_WRAM,
  F_HRAM,
  F_OAM,
  F_MMIO_REGS,
  F_BUS_CONFLICTS,
  F_OAM_DMA,
  F_VDMA,
  F_HAS_CART,
  F_CART,
};

void AddressBus::savestate_serialize(Savestate::Writer &out) const {
  constexpr std::size_t vram_bank_size = 0x2000;
  constexpr std::size_t wram_bank_size = 0x1000;
  constexpr std::size_t hram_size = 0x7F;
  constexpr std::size_t oam_size = 0xA0;

  out.field(F_VRAM, [&](Savestate::Writer &w) {
    for (const auto &bank : vram)
      w.bytes({bank.get(), vram_bank_size});
  });
  out.field(F_WRAM, [&](Savestate::Writer &w) {
    for (const auto &bank : wram)
      w.bytes({bank.get(), wram_bank_size});
  });
  out.field(F_HRAM, [&](Savestate::Writer &w) { w.bytes({hram.get(), hram_size}); });
  out.field(F_OAM, [&](Savestate::Writer &w) { w.bytes({oam.get(), oam_size}); });
  out.field(F_MMIO_REGS, [&](Savestate::Writer &w) {
    for (const auto &[addr, mmio] : io_registers) {
      if (mmio.savestate_policy != MMIOSavestatePolicy::BusAuto)
        continue;
      w.field(addr,
              [&](Savestate::Writer &mmio_w) {
                mmio.reg->savestate_serialize(mmio_w);
              });
    }
  });
  out.field_u32(F_BUS_CONFLICTS, bus_conflicts);

  out.field(F_OAM_DMA, [&](Savestate::Writer &w) { oam_dma.savestate_serialize(w); });
  out.field(F_VDMA, [&](Savestate::Writer &w) { vdma.savestate_serialize(w); });

  out.field_bool(F_HAS_CART, cart_ != nullptr);
  if (cart_)
    out.field(F_CART, [&](Savestate::Writer &w) { cart_->savestate_serialize(w); });
}

void AddressBus::savestate_deserialize(Savestate::Reader &in) {
  constexpr std::size_t vram_bank_size = 0x2000;
  constexpr std::size_t wram_bank_size = 0x1000;
  constexpr std::size_t hram_size = 0x7F;
  constexpr std::size_t oam_size = 0xA0;
  std::optional<bool> has_cart = std::nullopt;
  GBC_SS_DESERIALIZE_BEGIN(in)
  case F_VRAM:
    for (auto &bank : vram)
      payload.bytes({bank.get(), vram_bank_size});
    break;
  case F_WRAM:
    for (auto &bank : wram)
      payload.bytes({bank.get(), wram_bank_size});
    break;
  case F_HRAM:
    if (payload.remaining() != hram_size)
      throw std::runtime_error("AddressBus::savestate_deserialize() hram");
    payload.bytes({hram.get(), hram_size});
    break;
  case F_OAM:
    if (payload.remaining() != oam_size)
      throw std::runtime_error("AddressBus::savestate_deserialize() oam");
    payload.bytes({oam.get(), oam_size});
    break;
  case F_MMIO_REGS:
    while (const auto mmio_field = payload.next_field()) {
      auto [mmio_addr, mmio_payload] = *mmio_field;
      const auto it = io_registers.find(static_cast<addr_t>(mmio_addr));
      if (it != io_registers.end() &&
          it->second.savestate_policy == MMIOSavestatePolicy::BusAuto) {
        it->second.reg->savestate_deserialize(mmio_payload);
      } else {
        mmio_payload.skip(mmio_payload.remaining());
      }
      mmio_payload.expect_eof();
    }
    break;
  case F_BUS_CONFLICTS:
    bus_conflicts = static_cast<BusConflictTypes>(payload.u32());
    break;
  case F_OAM_DMA:
    oam_dma.savestate_deserialize(payload);
    break;
  case F_VDMA:
    vdma.savestate_deserialize(payload);
    break;
  GBC_SS_CASE_BOOL(F_HAS_CART, has_cart);
  case F_CART:
    if (!cart_)
      throw std::runtime_error("AddressBus::savestate_deserialize() no cart");
    cart_->savestate_deserialize(payload);
    break;
  GBC_SS_DESERIALIZE_END();

  if (has_cart.has_value()) {
    if (has_cart.value()) {
      if (!cart_)
        throw std::runtime_error("AddressBus::savestate_deserialize() no cart");
    } else if (cart_) {
      throw std::runtime_error("AddressBus::savestate_deserialize() cart mismatch");
    }
  }
}
