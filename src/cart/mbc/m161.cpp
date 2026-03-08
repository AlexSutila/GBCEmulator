#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

// ---------------------------
// M161 (32 KiB multicart, one-time bank switch)
// ---------------------------
// Pan Docs: https://gbdev.io/pandocs/M161.html
//
// Memory:
// ROM bank (32 KiB banks 00-07)          (0000-7FFF)
//
// Registers (write only):
// ROM bank number (00-07)                (0000-7FFF)
// Notes:
// - Entire 0000-7FFF region is switched as a single 32 KiB bank.
// - Only 1 bank switch is allowed per power session; further writes are ignored.

class M161 final : public Mbc {
public:
  explicit M161(std::span<const byte_t> const rom) : rom_(rom) {}

  byte_t read(addr_t const addr) override {
    if (addr <= 0x7FFF) {
      const std::size_t idx = bank_base_32k() + static_cast<std::size_t>(addr);
      return idx < rom_.size() ? rom_[idx] : open_bus();
    }
    return open_bus();
  }

  void write(addr_t const addr, byte_t const val) override {
    if (addr > 0x7FFF)
      return;

    if (latched_)
      return;

    bank_ = static_cast<byte_t>(val & 0x07);
    latched_ = true; // Any write consumes the single allowed bank switch.
  }

private:
  std::span<const byte_t> rom_;

  static constexpr std::size_t kBank32k = 0x8000;

  byte_t bank_{0};
  bool latched_{false};

  [[nodiscard]] std::size_t bank_count_32k() const {
    return std::max<std::size_t>(1, rom_.size() / kBank32k);
  }

  [[nodiscard]] std::size_t bank_base_32k() const {
    const std::size_t b = clamp_bank(bank_, bank_count_32k());
    return b * kBank32k;
  }
};

std::unique_ptr<Mbc> make_m161(const cart &c) {
  return std::make_unique<M161>(c.rom_span());
}
