#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

// ---------------------------
// MBC1 / MBC1M
// ---------------------------
// RAM enable (0000-1FFF)
// ROM bank (2000-3FFF)
// Upper bits/RAM bank (4000-5FFF)
// mode (6000-7FFF)

class Mbc1 final : public Mbc {
public:
  Mbc1(const std::span<const byte_t> rom, const std::size_t ram_bytes,
       const bool battery, const bool is_mbc1m)
      : rom_(rom), ram_(ram_bytes), battery_(battery), is_mbc1m_(is_mbc1m) {}

  byte_t read(const addr_t addr) override {
    if (addr <= 0x3FFF) {
      const std::size_t bank0 = mode_ ? (static_cast<std::size_t>(upper2_ & 0x03) << bank2_shift_()) : 0;
      return rom_at(rom_, bank0, addr);
    }
    if (addr <= 0x7FFF) {
      const std::size_t bank = effective_rom_bank();
      return rom_at(rom_, bank, addr - 0x4000);
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_enabled_ || ram_.empty())
        return open_bus(); // RAM only accessible if enabled
      const std::size_t rbank = mode_ && !is_mbc1m_ ? upper2_ & 0x03 : 0;
      return ram_at(rbank, addr - 0xA000);
    }
    return open_bus();
  }

  void write(const addr_t addr, const byte_t val) override {
    if (addr <= 0x1FFF) {
      ram_enabled_ =
          (val & 0x0F) == 0x0A; // any value with low nibble A (0101) enables
      return;
    }
    if (addr <= 0x3FFF) {
      rom_bank1_ = static_cast<byte_t>(val & 0x1F);   // keep only low 5 bits
      return;
    }
    if (addr <= 0x5FFF) {
      upper2_ = val & 0x03;
      return;
    }
    if (addr <= 0x7FFF) {
      mode_ = val & 0x01; // 0=simple, 1=advanced
      return;
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_enabled_ || ram_.empty())
        return;
      const std::size_t rbank = (mode_ ? (upper2_ & 0x03) : 0);
      ram_write(rbank, addr - 0xA000, val);
      return;
    }
  }

  [[nodiscard]] bool has_battery() const noexcept override { return battery_; }
  [[nodiscard]] std::span<const byte_t> ram() const noexcept override { return ram_; }
  std::span<byte_t> ram() noexcept override { return ram_; }

private:
  byte_t upper2_{0b00}; // BANK2: upper 2 bits of rom bank number or ram bank
                        // number, depending on mode
  byte_t mode_{0b0};    // MODE: 1: BANK2 affects 0x0000-0x3FFF, 0x4000-0x7FFF,
                        // 0xA000-0xBFFF/ 0: only 0x4000-0x7FFF
  bool ram_enabled_{false}; // RAMG: 0b1010 enables, other values disables. This
                            // represents the state after writing
  byte_t rom_bank1_{0x01};

  std::span<const byte_t> rom_;
  std::vector<byte_t> ram_;
  bool battery_{};
  bool is_mbc1m_{false};

  [[nodiscard]] std::size_t effective_rom_bank() const {
    const std::size_t hi =
        static_cast<std::size_t>(upper2_ & 0x03) << bank2_shift_();
    const auto lo = static_cast<std::size_t>(bank1_low_for_addr_());
    return hi | lo;
  }

  [[nodiscard]] byte_t ram_at(const std::size_t bank, const std::size_t off) const {
    if (ram_.empty())
      return open_bus();
    const std::size_t banks =
        std::max<std::size_t>(1, ram_.size() / kRamBankSize);
    const std::size_t b = clamp_bank(bank, banks);

    // Unmapped bank access typically wraps
    const std::size_t idx = (b * kRamBankSize + off) % ram_.size();
    return ram_[idx];
  }

  void ram_write(const std::size_t bank, const std::size_t off,
                 const byte_t v) {
    if (ram_.empty())
      return;
    const std::size_t banks =
        std::max<std::size_t>(1, ram_.size() / kRamBankSize);
    const std::size_t b = clamp_bank(bank, banks);
    const std::size_t idx = (b * kRamBankSize + off) % ram_.size();
    ram_[idx] = v;
  }

  [[nodiscard]] std::size_t bank2_shift_() const { return is_mbc1m_ ? 4 : 5; }

  [[nodiscard]] byte_t bank1_low_for_addr_() const {
    // 00→01 translation depends on the full 5-bit BANK1 value
    auto v = static_cast<byte_t>(rom_bank1_ & 0x1F);
    if (v == 0)
      v = 1;

    // In MBC1M, bit4 of BANK1 is physically not connected for addressing
    return is_mbc1m_ ? static_cast<byte_t>(v & 0x0F) : v;
  }
};

std::unique_ptr<Mbc> make_mbc1(const cart& c) {
  const bool battery = type_has_battery(c.header.cartridge_type);
  return std::make_unique<Mbc1>(c.rom_span(), c.declared_ram_bytes, battery, c.special_mbc == MBC1M_t);
}
