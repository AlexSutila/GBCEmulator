#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"
#include "savestate/codec.hpp"

// ---------------------------
// MBC5
// ---------------------------
// RAM enable (0000-1FFF)
// ROM bank low 8 (2000-2FFF)
// ROM bank bit 9 (3000-3FFF)
// RAM bank (4000-5FFF)
// Rumble + guaranteed timing in CGB double speed

class Mbc5 final : public Mbc {
public:
  Mbc5(std::span<const byte_t> const rom, std::size_t const ram_bytes,
       bool const battery, bool const rumble)
      : rom_(rom), ram_(ram_bytes), battery_(battery), rumble_(rumble) {}

  byte_t read(addr_t const addr) override {
    if (addr <= 0x3FFF)
      return rom_at(rom_, 0, addr);
    if (addr <= 0x7FFF)
      return rom_at(rom_, rom_bank_, addr - 0x4000);

    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_enabled_ || ram_.empty())
        return open_bus();
      const std::size_t bank = clamp_bank(
          ram_bank_, std::max<std::size_t>(1, ram_.size() / kRamBankSize));
      return ram_[(bank * kRamBankSize + (addr - 0xA000)) % ram_.size()];
    }
    return open_bus();
  }

  void write(addr_t const addr, byte_t const val) override {
    if (addr <= 0x1FFF) {
      ram_enabled_ =
          ((val & 0x0F) == 0x0A); // Real MBCs accept any low-nibble A
      return;
    }
    if (addr <= 0x2FFF) {
      rom_bank_ =
          (rom_bank_ & 0x100) |
          val; // low 8 bits        // Writing zero is OK unlike other MBCs
      return;
    }
    if (addr <= 0x3FFF) {
      rom_bank_ = (rom_bank_ & 0x0FF) | ((val & 0x01) << 8); // 9th bit
      return;
    }
    if (addr <= 0x5FFF) {
      // On rumble carts, bit3 is rumble enable instead of RAM address line
      if (rumble_) {
        rumble_on_ = (val & 0x08) != 0;
        ram_bank_ = (val & 0x07);
      } else {
        ram_bank_ = (val & 0x0F);
      }
      return;
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_enabled_ || ram_.empty())
        return;
      const std::size_t banks =
          std::max<std::size_t>(1, ram_.size() / kRamBankSize);
      const std::size_t bank = clamp_bank(ram_bank_, banks);
      ram_[(bank * kRamBankSize + (addr - 0xA000)) % ram_.size()] = val;
    }
  }

  [[nodiscard]] bool has_battery() const noexcept override { return battery_; }
  [[nodiscard]] std::span<const byte_t> ram() const noexcept override {
    return ram_;
  }
  std::span<byte_t> ram() noexcept override { return ram_; }

  template <typename T> void parse_savestate_impl(T &t) {
    constexpr auto version = 1; // Schema revision
    t.chunk_header(version, Savestate::C_MBC_5);
    t.field_generic(F_RAM_ENABLED, ram_enabled_);
    t.field_generic(F_ROM_BANK, rom_bank_);
    t.field_generic(F_RAM_ENABLED, ram_bank_);
    t.field_generic(F_RUMBLE_ON, rumble_on_);
    t.eof();
  }

  void parse_savestate(Savestate::Writer &t) override {
    parse_savestate_impl(t);
  }
  void parse_savestate(Savestate::Reader &t) override {
    parse_savestate_impl(t);
  }
  void parse_savestate(Savestate::Sizer &t) override {
    parse_savestate_impl(t);
  }

private:
  std::span<const byte_t> rom_;
  std::vector<byte_t> ram_;
  bool battery_{};
  bool rumble_{}; // This controls whether rumble is enabled and is set by
                  // cartridge during init

  bool ram_enabled_{false};       // 0b1010 enables like other MBCs. This is the
                                  // result after the check
  std::uint16_t rom_bank_{0x001}; // 9-bit
  std::size_t ram_bank_{0x00};
  bool rumble_on_{false}; // This is the physical state of rumble. Turning it on
                          // has no effect. If we somehow port it to a handset
                          // then this can be hooked up to some motors

  enum : std::uint16_t {
    F_RAM_ENABLED = 1,
    F_ROM_BANK,
    F_RAM_BANK,
    F_RUMBLE_ON
  };
};

std::unique_ptr<Mbc> make_mbc5(const cart &c) {
  const bool battery = type_has_battery(c.header.cartridge_type);
  const bool rumble =
      (c.header.cartridge_type == 0x1C || // MBC5+RUMBLE
       c.header.cartridge_type == 0x1D || // MBC5+RUMBLE+RAM
       c.header.cartridge_type == 0x1E || // MBC5+RUMBLE+RAM+BATTERY
       c.header.cartridge_type == 0x22);  // MBC7+SENSOR+RUMBLE+RAM+BATTERY

  return std::make_unique<Mbc5>(c.rom_span(), c.declared_ram_bytes, battery,
                                rumble);
}
