#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

// ---------------------------
// MMM01 (multi-game compilation mapper; MBC1-like with "unmapped" menu mode)
// ---------------------------
// Pan Docs: https://gbdev.io/pandocs/MMM01.html
//
// Memory (mapped mode):
// 0000-3FFF: "ROM Bank X0" region (varies with multiplex/mode)
// 4000-7FFF: switchable, but bank 00/20/40/60 within-game are remapped to 01/21/41/61
// A000-BFFF: banked RAM, depends on mode + multiplex; unknown if accessible in unmapped mode
//
// Key behavior:
// - Starts in "unmapped" mode: last 32 KiB of ROM is always mapped at 0000-7FFF
// - Enter "mapped" mode by writing bit6=1 in 0000-1FFF (Mapping Enable)
// - In mapped mode, extended bits are no longer writeable; mapper behaves like MBC1,
//   but the latched extended bits still contribute to the full bank number

class Mmm01 final : public Mbc {
public:
  Mmm01(std::span<const byte_t> const rom, std::size_t const ram_bytes,
        bool const battery)
      : rom_(rom), ram_(ram_bytes), battery_(battery) {}

  byte_t read(addr_t const addr) override {
    if (addr <= 0x7FFF) {
      if (!mapped_) {
        // Unmapped mode: always map last 32 KiB of ROM to 0000-7FFF
        if (rom_.size() < 0x8000)
          return open_bus();
        const std::size_t base = rom_.size() - 0x8000;
        const std::size_t idx = base + static_cast<std::size_t>(addr);
        return (idx < rom_.size()) ? rom_[idx] : open_bus();
      }

      if (addr <= 0x3FFF)
        return rom_at(rom_, rom_bank_0000(), addr);
      return rom_at(rom_, rom_bank_4000(), addr - 0x4000);
    }

    // External RAM
    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_enabled_ || ram_.empty())
        return open_bus();

      // Pan Docs notes it's unknown if RAM is accessible in unmapped mode
      // In practice, letting it work improves compatibility with the one known
      // RAM-containing MMM01 cart, and doesn't affect most carts
      const std::size_t bank = ram_bank_a000();
      const std::size_t idx =
          (bank * kRamBankSize + (addr - 0xA000)) % std::max<std::size_t>(1, ram_.size());
      return ram_[idx];
    }

    return open_bus();
  }

  void write(addr_t const addr, byte_t const val) override {
    // 0000-1FFF: RAM Enable + (unmapped-only) RAM Bank Mask + Mapping Enable 
    if (addr <= 0x1FFF) {
      ram_enabled_ = ((val & 0x0F) == 0x0A);

      if (!mapped_) {
        ram_bank_mask_ = static_cast<byte_t>((val >> 4) & 0x03);

        // Enter mapped mode when bit6 is set
        if ((val & 0x40) != 0)
          mapped_ = true;
      }
      return;
    }

    // 2000-3FFF: ROM Bank Low + (unmapped-only) ROM Bank Mid 
    if (addr <= 0x3FFF) {
      set_rom_bank_low_locked(static_cast<byte_t>(val & 0x1F));

      if (!mapped_) {
        rom_bank_mid_ = static_cast<byte_t>((val >> 5) & 0x03);
      }
      return;
    }

    // 4000-5FFF: RAM Bank Low + (unmapped-only) RAM Bank High + ROM Bank High + mode write lock 
    if (addr <= 0x5FFF) {
      set_ram_bank_low_locked(static_cast<byte_t>(val & 0x03));

      if (!mapped_) {
        ram_bank_high_ = static_cast<byte_t>((val >> 2) & 0x03);
        rom_bank_high_ = static_cast<byte_t>((val >> 4) & 0x03);
        mode_write_lock_ = ((val & 0x40) != 0);
      }
      return;
    }

    // 6000-7FFF: mode select + (unmapped-only) ROM Bank Mask + multiplex enable 
    if (addr <= 0x7FFF) {
      if (!mode_write_lock_) {
        mbc1_mode_ = ((val & 0x01) != 0);
      }

      if (!mapped_) {
        // Bits 1-5 are mask bits; lowest mask bit is ignored (forced 0)
        auto mask = static_cast<byte_t>((val >> 1) & 0x1F);
        mask = static_cast<byte_t>(mask & 0x1E);
        rom_bank_mask_ = mask;

        multiplex_ = ((val & 0x40) != 0);
      }
      return;
    }

    // A000-BFFF: RAM write
    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_enabled_ || ram_.empty())
        return;
      const std::size_t bank = ram_bank_a000();
      const std::size_t idx =
          (bank * kRamBankSize + (addr - 0xA000)) % std::max<std::size_t>(1, ram_.size());
      ram_[idx] = val;
      return;
    }
  }

  [[nodiscard]] bool has_battery() const noexcept override { return battery_; }
  [[nodiscard]] std::span<const byte_t> ram() const noexcept override { return ram_; }
  std::span<byte_t> ram() noexcept override { return ram_; }
  [[nodiscard]] const char *savestate_tag() const noexcept override {
    return "MMM1";
  }

private:
  std::span<const byte_t> rom_;
  std::vector<byte_t> ram_;
  bool battery_{};

  // State
  bool mapped_{false};          // "unmapped" at power-on
  bool ram_enabled_{false};     // low nibble == A enables

  bool multiplex_{false};       // mode reg bit6 (unmapped-only)
  bool mbc1_mode_{false};       // mode reg bit0
  bool mode_write_lock_{false}; // prevents changes to mbc1_mode_ 

  // Registers (7-bit, but we store only used fields)
  byte_t ram_bank_mask_{0};     // 2-bit write-lock mask for RAM Bank Low
  byte_t rom_bank_mask_{0};     // 5-bit write-lock mask for ROM Bank Low (bit0 forced 0)

  byte_t rom_bank_low_{0x01};   // behaves like $01 on power-up
  byte_t rom_bank_mid_{0x00};   // unmapped-only
  byte_t rom_bank_high_{0x00};  // unmapped-only

  byte_t ram_bank_low_{0x00};   // MBC1 RAM bank reg (or swapped in multiplex)
  byte_t ram_bank_high_{0x00};  // unmapped-only

  void set_rom_bank_low_locked(byte_t const new_low) {
    // ROM Bank Mask prevents writes to matching bits of ROM Bank Low
    // Mask bit0 is always 0, so bit0 is always writable
    const auto lock = static_cast<byte_t>(rom_bank_mask_ & 0x1F);
    const auto keep = static_cast<byte_t>(rom_bank_low_ & lock);
    const auto take = static_cast<byte_t>(new_low & static_cast<byte_t>(~lock) & 0x1F);
    rom_bank_low_ = static_cast<byte_t>((keep | take) & 0x1F);
  }

  void set_ram_bank_low_locked(byte_t const new_low) {
    // RAM Bank Mask prevents writes to matching bits of RAM Bank Low
    const auto lock = static_cast<byte_t>(ram_bank_mask_ & 0x03);
    const auto keep = static_cast<byte_t>(ram_bank_low_ & lock);
    const auto take = static_cast<byte_t>(new_low & static_cast<byte_t>(~lock) & 0x03);
    ram_bank_low_ = static_cast<byte_t>((keep | take) & 0x03);
  }

  [[nodiscard]] byte_t rom_bank_low_for_0000() const {
    // 0000-3FFF uses only the “game select” bits of ROM Bank Low: ROM Bank Low & ROM Bank Mask
    return static_cast<byte_t>((rom_bank_low_ & rom_bank_mask_) & 0x1F);
  }

  [[nodiscard]] byte_t rom_bank_low_for_4000() const {
    // 4000-7FFF uses ROM Bank Low (complete), but bank $00/$20/$40/$60 are remapped
    // by forcing low bit if the unmasked bits are 0
    auto low = static_cast<byte_t>(rom_bank_low_ & 0x1F);
    if (const auto unmasked = static_cast<byte_t>(low & static_cast<byte_t>(~rom_bank_mask_) & 0x1F); unmasked == 0)
      low = static_cast<byte_t>(low | 0x01);
    return low;
  }

  [[nodiscard]] std::size_t rom_bank_0000() const {
    const auto hi = static_cast<std::size_t>(rom_bank_high_ & 0x03);

    if (!multiplex_) {
      const auto mid = static_cast<std::size_t>(rom_bank_mid_ & 0x03);
      const auto low = static_cast<std::size_t>(rom_bank_low_for_0000());
      return (hi << 7) | (mid << 5) | low; // mapped, multiplex disabled 
    }

    // multiplex enabled: mid-bits come from RAM Bank Low, masked in mode0, full in mode1
    const auto rb_mask = static_cast<byte_t>(ram_bank_mask_ & 0x03);
    const std::size_t mid = mbc1_mode_
        ? static_cast<std::size_t>(ram_bank_low_ & 0x03)                  // mode1
        : static_cast<std::size_t>((ram_bank_low_ & rb_mask) & 0x03);     // mode0 

    const auto low = static_cast<std::size_t>(rom_bank_low_for_0000());
    return (hi << 7) | (mid << 5) | low;
  }

  [[nodiscard]] std::size_t rom_bank_4000() const {
    const auto hi = static_cast<std::size_t>(rom_bank_high_ & 0x03);

    if (!multiplex_) {
      const auto mid = static_cast<std::size_t>(rom_bank_mid_ & 0x03);
      const auto low = static_cast<std::size_t>(rom_bank_low_for_4000());
      return (hi << 7) | (mid << 5) | low; // mapped, multiplex disabled 
    }

    // multiplex enabled: 4000-7FFF uses RAM Bank Low complete, independent of mode 
    const auto mid = static_cast<std::size_t>(ram_bank_low_ & 0x03);
    const auto low = static_cast<std::size_t>(rom_bank_low_for_4000());
    return (hi << 7) | (mid << 5) | low;
  }

  [[nodiscard]] std::size_t ram_bank_a000() const {
    if (ram_.empty())
      return 0;

    const auto hi = static_cast<std::size_t>(ram_bank_high_ & 0x03);

    if (multiplex_) {
      // multiplex enabled: low bits come from ROM Bank Mid 
      const auto low = static_cast<std::size_t>(rom_bank_mid_ & 0x03);
      const std::size_t bank = (hi << 2) | low;
      const std::size_t banks = std::max<std::size_t>(1, ram_.size() / kRamBankSize);
      return clamp_bank(bank, banks);
    }

    // multiplex disabled:
    // mode0: RAM Bank Low & RAM Bank Mask
    // mode1: RAM Bank Low full 
    const auto rb_mask = static_cast<byte_t>(ram_bank_mask_ & 0x03);
    const std::size_t low = mbc1_mode_
        ? static_cast<std::size_t>(ram_bank_low_ & 0x03)
        : static_cast<std::size_t>((ram_bank_low_ & rb_mask) & 0x03);

    const std::size_t bank = (hi << 2) | low;
    const std::size_t banks = std::max<std::size_t>(1, ram_.size() / kRamBankSize);
    return clamp_bank(bank, banks);
  }
};

std::unique_ptr<Mbc> make_mmm01(const cart &c) {
  const bool battery = type_has_battery(c.header.cartridge_type);
  return std::make_unique<Mmm01>(c.rom_span(), c.declared_ram_bytes, battery);
}
