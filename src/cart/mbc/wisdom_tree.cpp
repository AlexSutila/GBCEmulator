#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

// ---------------------------
// Wisdom Tree mapper (unlicensed)
// ---------------------------
// - Banks the whole 32 KiB region 0000-7FFF
// - Bank select ignores data; uses low 8 bits of address written to (A7-A0)
//   Write any value to address YYXX (YY in 00-7F) to select bank XX.
//
// No documented external RAM for this mapper.

class WisdomTree final : public Mbc {
public:
  explicit WisdomTree(const std::span<const byte_t> rom)
      : rom_(rom) {}

  byte_t read(addr_t const addr) override {
    if (addr <= 0x7FFF) {
      return rom_at_32k(bank_, addr);
    }
    // No RAM / no other mapped regions for WT.
    return open_bus();
  }

  void write(addr_t const addr, byte_t const /*val*/) override {
    // Any write in 0000-7FFF updates bank = A7-A0 of the address.
    if (addr <= 0x7FFF) {
      bank_ = static_cast<byte_t>(addr & 0xFF);
    }
  }

  [[nodiscard]] const char *savestate_tag() const noexcept override {
    return "WTRE";
  }

private:
  static constexpr std::size_t kBankSize32k = 0x8000;

  std::span<const byte_t> rom_;
  byte_t bank_{0}; // HW power-on is undefined; 0 is a default

  [[nodiscard]] std::size_t bank_count_32k() const noexcept {
    return std::max<std::size_t>(1, (rom_.size() + (kBankSize32k - 1)) / kBankSize32k);
  }

  [[nodiscard]] byte_t rom_at_32k(std::size_t const bank, std::size_t const off) const {
    const std::size_t banks = bank_count_32k();
    const std::size_t b = clamp_bank(bank, banks);
    const std::size_t idx = b * kBankSize32k + off;
    return (idx < rom_.size()) ? rom_[idx] : open_bus();
  }
};

std::unique_ptr<Mbc> make_wisdom_tree(const cart& c) {
  return std::make_unique<WisdomTree>(c.rom_span());
}
