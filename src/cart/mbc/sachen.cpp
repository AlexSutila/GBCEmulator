#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"
#include "savestate/codec.hpp"

// When A8 is high, while A15..A9 are low, the mapper will jumble the address
bool sachen_should_jumble(addr_t addr) noexcept { return (addr & 0xFF00) == 0x0100; }

// Sachen chose to scramble the header of their games. When A8 is high, while
// A15..A9 are low, the mapper will perform the following map:
//  - RA0 <= A6
//  - RA1 <= A4
//  - RA4 <= A1
//  - RA6 <= A0
// We re-use the same code in our heuristic to determine if we should force
// this mapper type by using it to look for a valid jumbled logo checksum.
addr_t sachen_jumble(addr_t addr) noexcept {
  addr_t unjumbled_addr = addr & 0xFFAC;
  if ((addr & 0x40) != 0) // Bit 6 -> 0
    unjumbled_addr = unjumbled_addr | 0x01;
  if ((addr & 0x10) != 0) // Bit 4 -> 1
    unjumbled_addr = unjumbled_addr | 0x02;
  if ((addr & 0x02) != 0) // Bit 1 -> 4
    unjumbled_addr = unjumbled_addr | 0x10;
  if ((addr & 0x01) != 0) // Bit 0 -> 6
    unjumbled_addr = unjumbled_addr | 0x40;
  return unjumbled_addr;
}

// -----------------------------
// Sachen MMC2 (I hate this MBC)
// -----------------------------
// Sachen's MMC2 can be used to address up to 32 Mbit of ROM.
class Sachen final : public Mbc {
public:
  Sachen(std::span<const byte_t> const rom) : rom_(rom) {}

  byte_t read(addr_t addr) override {
    addr_t phys = addr;

    if (addr < 0x8000 && sachen_should_jumble(addr))
      phys = sachen_jumble(addr);

    // I dont think this mapper includes ram, docs say >=0x8000 is unmapped
    if (addr <= 0x3FFF) {
      byte_t bank = remap_(0);
      return rom_at(rom_, bank, phys & 0x3FFF);
    }
    if (addr <= 0x7FFF) {
      byte_t bank = remap_(rom_bank);
      return rom_at(rom_, bank, phys & 0x3FFF);
    }
    return open_bus();
  }

  void write(addr_t addr, byte_t val) override {
    if (addr <= 0x1FFF) {
      if (internal_regs_write_granted_())
        base_rom_bank = val;
      return;
    }
    if (addr <= 0x3FFF) {
      rom_bank = val;
      if (rom_bank == 0)
        ++rom_bank;
      return;
    }
    if (addr <= 0x5FFF) {
      if (internal_regs_write_granted_())
        rom_bank_mask = val;
      return;
    }
    // writes to remaining area are unmapped
  }

  template <typename T> void parse_savestate_impl(T &t) {
    constexpr auto version = 1; // Schema revision
    t.chunk_header(version, Savestate::C_MBC_SACHEN);
    t.field_generic(F_BASE_ROM_BANK, base_rom_bank);
    t.field_generic(F_ROM_BANK_MASK, rom_bank_mask);
    t.field_generic(F_ROM_BANK, rom_bank);
    t.eof();
  }

  void parse_savestate(Savestate::Writer &t) override { parse_savestate_impl(t); }
  void parse_savestate(Savestate::Reader &t) override { parse_savestate_impl(t); }
  void parse_savestate(Savestate::Sizer &t) override { parse_savestate_impl(t); }
  void parse_savestate(Savestate::Checker &t) override { parse_savestate_impl(t); }

private:
  std::span<const byte_t> rom_;
  byte_t base_rom_bank{0x00};
  byte_t rom_bank_mask{0x00};
  byte_t rom_bank{0x01};

  enum : std::uint16_t {
    F_BASE_ROM_BANK = 1,
    F_ROM_BANK_MASK,
    F_ROM_BANK,
  };

  [[nodiscard]] addr_t remap_(byte_t bank) const noexcept {
    return (bank & ~rom_bank_mask) | (rom_bank_mask & base_rom_bank);
  }

  // If either of these bits are not set, writes to the other two internal registers
  // are not granted, and will be ignored.
  [[nodiscard]] bool internal_regs_write_granted_() const noexcept {
    return (rom_bank & 0x30) == 0x30;
  }
};

std::unique_ptr<Mbc> make_sachen(const cart &c) {
  // TODO: Does this mapper actually provide SRAM? If so how to detect?
  return std::make_unique<Sachen>(c.rom_span());
}
