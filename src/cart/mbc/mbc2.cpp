#include "cart/mbc.hpp"

// ---------------------------
// MBC2
// ---------------------------
// Internal 512 x 4-bit RAM at A000–A1FF with echoes
// Writes in 0000–3FFF use addr bit 8 to pick RAM-enable vs ROM-bank

class Mbc2 final : public Mbc {
public:
  Mbc2(std::span<const byte_t> const rom, bool const battery)
      : rom_(rom), battery_(battery) {
    ram_.fill(0x00);
  }

  byte_t read(addr_t const addr) override {
    if (addr <= 0x3FFF) {
      return rom_at(0, addr);
    }
    if (addr <= 0x7FFF) {
      return rom_at(rom_bank_, addr - 0x4000);
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_enabled_)
        return open_bus();
      const std::size_t idx =
          (addr - 0xA000) & 0x01FF; // bottom 9 bits (echo behavior)
      const byte_t nib = (ram_[idx] & 0x0F);
      return static_cast<byte_t>(0xF0 | nib); // upper nibble undefined
    }
    return open_bus();
  }

  void write(addr_t const addr, byte_t const val) override {
    if (addr <= 0x3FFF) {
      if (const bool bit8 = (addr & 0x0100) != 0; !bit8) {
        ram_enabled_ =
            ((val & 0x0F) == 0x0A); // low nibble A (0101) enables RAM
      } else {
        byte_t bank = (val & 0x0F);
        if (bank == 0)
          bank = 1; // 0 -> 1
        rom_bank_ = bank;
      }
      return;
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_enabled_)
        return;
      const std::size_t idx = (addr - 0xA000) & 0x01FF;
      ram_[idx] = static_cast<byte_t>(val & 0x0F); // only low 4 bits stored
    }
  }

  bool has_battery() const noexcept override { return battery_; }

  // expose "RAM" as 512 bytes (low nibble meaningful)
  std::span<const byte_t> ram() const noexcept override {
    return std::span<const byte_t>(ram_.data(), ram_.size());
  }
  std::span<byte_t> ram() noexcept override {
    return std::span<byte_t>(ram_.data(), ram_.size());
  }

private:
  std::span<const byte_t> rom_;
  std::array<byte_t, 512> ram_{};
  bool battery_{};

  bool ram_enabled_{false}; // RAMG: 0b1010 enables, other values disables. This
                            // represents the state after writing
  byte_t rom_bank_{
      1}; // ROMB: 4-bit for ROM bank number. Never contain zero value

  byte_t rom_at(std::size_t const bank, std::size_t const off) const {
    const auto banks = rom_bank_count(rom_);
    const auto b = clamp_bank(bank, banks);
    const std::size_t idx = b * kRomBankSize + off;
    return (idx < rom_.size()) ? rom_[idx] : open_bus();
  }
};
