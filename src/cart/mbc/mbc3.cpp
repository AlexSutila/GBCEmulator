#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"
#include "savestate/codec.hpp"

// ---------------------------
// MBC3 (ROM/RAM + RTC)
// ---------------------------
// RAM+Timer enable (0000-1FFF)
// ROM bank (2000-3FFF)
// RAM bank or RTC reg select (4000-5FFF)
// Latch clock data 00->01 (6000-7FFF)
// gekkio has a stub entry for MBC3 only, going off the Pan Docs for this one

class Mbc3 final : public Mbc {
public:
  Mbc3(const std::span<const byte_t> rom, std::size_t const ram_bytes,
       bool const battery, bool const has_rtc, bool const is_mbc30)
      : rom_(rom), ram_(ram_bytes), battery_(battery), has_rtc_(has_rtc),
        is_mbc30_(is_mbc30) {}

  // TODO: currently there is no mechanism to keep the clock ticking after the
  // emulator is shut down. It is reasonable to calculate the delta between now
  // and "last power off" and apply the delta on boot. More research needed
  void tick(std::chrono::seconds const elapsed) override {
    if (!has_rtc_)
      return;
    if (rtc_halt_)
      return;
    advance_rtc(static_cast<std::uint32_t>(elapsed.count()));
  }

  byte_t read(addr_t const addr) override {
    if (addr <= 0x3FFF)
      return rom_at(rom_, 0, addr);
    if (addr <= 0x7FFF)
      return rom_at(rom_, rom_bank_, addr - 0x4000);

    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_rtc_enabled_)
        return open_bus();

      if (sel_ <= 0x07) {
        if (ram_.empty())
          return open_bus();
        return ram_at(sel_, addr - 0xA000);
      }
      if (sel_ >= 0x08 && sel_ <= 0x0C && has_rtc_) {
        const RtcRegs &r = latched_valid_ ? latched_ : rtc_;
        return rtc_reg_read(r, sel_);
      }
      return open_bus();
    }

    return open_bus();
  }

  void write(addr_t const addr, byte_t const val) override {
    if (addr <= 0x1FFF) {
      ram_rtc_enabled_ = ((val & 0x0F) == 0x0A); // 0b1010 enables RAM + RTC
      return;
    }
    if (addr <= 0x3FFF) {
      const byte_t mask = is_mbc30_ ? 0xFF : 0x7F; // MBC30/MBC3
      byte_t b = val & mask;
      if (b == 0)
        b = 1; // force 0b00 to 0b01, similar to MBC1
      rom_bank_ = b;
      return;
    }
    if (addr <= 0x5FFF) {
      if (val <= 0x07) {
        const byte_t mask = is_mbc30_ ? 0x07 : 0x03;
        sel_ = val & mask;
      } else {
        sel_ = val; // 00-07 RAM bank, 08-0C RTC reg
      }
      return;
    }
    if (addr <= 0x7FFF) {
      // latch on 00 -> 01 transition
      if (latch_prev_ == 0x00 && val == 0x01) {
        latched_ = rtc_;
        latched_valid_ = true;
      }
      latch_prev_ = val;
      return;
    }

    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!ram_rtc_enabled_)
        return;

      if (sel_ <= 0x07) {
        if (ram_.empty())
          return;
        ram_write(sel_, addr - 0xA000, val);
        return;
      }
      if (sel_ >= 0x08 && sel_ <= 0x0C && has_rtc_) {
        rtc_reg_write(rtc_, sel_, val);
        // TODO: Pan Docs says set Halt before writing RTC regs, need to decide
        // what to do
      }
    }
  }

  [[nodiscard]] bool has_battery() const noexcept override { return battery_; }
  [[nodiscard]] std::span<const byte_t> ram() const noexcept override {
    return ram_;
  }
  std::span<byte_t> ram() noexcept override { return ram_; }

  template <typename T> void parse_savestate_impl(T &t) {
    constexpr auto version = 1; // Schema revision
    t.chunk_header(version, Savestate::C_MBC_3);
    t.field_generic(F_RAM_RTC_ENABLED, ram_rtc_enabled_);
    t.field_generic(F_ROM_BANK, rom_bank_);
    t.field_generic(F_SEL, sel_);
    t.field_generic(F_LATCH_PREV, latch_prev_);
    t.field_generic(F_LATCH_PREV, latch_prev_);

    const auto write_rtc = [&](T &t, RtcRegs &r) {
      t.field_generic(1, r.sec);
      t.field_generic(2, r.min);
      t.field_generic(3, r.hour);
      t.field_generic(4, r.day);
      t.field_generic(5, r.halt);
      t.field_generic(6, r.carry);
    };
    t.field_complex(F_RTC, [&](T &t) { write_rtc(t, rtc_); });
    t.field_complex(F_LATCHED_RTC, [&](T &t) { write_rtc(t, latched_); });
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
  bool has_rtc_{};
  bool is_mbc30_{};

  bool ram_rtc_enabled_{false}; // RAMR? 0b1010 enables RAM and RTC
  byte_t rom_bank_{
      0b0000001}; // ROMB except all 7 bits are used; zero value disallowed
  byte_t sel_{0}; // RAM bank or RTC reg selector
  byte_t latch_prev_{0}; // Latch clock data

  struct RtcRegs {       // Implements RTC Register 08-0C. Note: this is an
                   // abstraction, not a 1-to-1 replication of hw reg behavior
    byte_t sec{0}, min{0}, hour{0};
    std::uint16_t day{0};
    bool halt{false};
    bool carry{false};
  };

  RtcRegs rtc_{};
  RtcRegs latched_{};
  bool latched_valid_{false};
  bool &rtc_halt_ = rtc_.halt;

  enum : std::uint16_t {
    F_RAM_RTC_ENABLED = 1,
    F_ROM_BANK,
    F_SEL,
    F_LATCH_PREV,
    F_LATCHED_VALID,
    F_RTC,
    F_LATCHED_RTC
  };

  static byte_t rtc_reg_read(const RtcRegs &r, byte_t const reg) {
    switch (reg) {
    case 0x08:
      return r.sec; // 0-59
    case 0x09:
      return r.min; // 0-59
    case 0x0A:
      return r.hour; // 0-23
    case 0x0B:
      return static_cast<byte_t>(r.day & 0xFF); // DL (day low) bits
    // Bit 7 |  6   | 5 | 4 | 3 | 2 | 1 | 0
    // Carry | Halt | U | U | U | U | U | DH
    case 0x0C: {
      byte_t v = 0;
      v |= static_cast<byte_t>((r.day >> 8) & 0x01); // DH (day high) bit
      if (r.halt)
        v |= 0x40;
      if (r.carry)
        v |= 0x80;
      return v;
    }
    default:
      return open_bus();
    }
  }

  static void rtc_reg_write(RtcRegs &r, byte_t const reg, byte_t const val) {
    switch (reg) {
    case 0x08:
      r.sec = static_cast<byte_t>(val % 60);
      break;
    case 0x09:
      r.min = static_cast<byte_t>(val % 60);
      break;
    case 0x0A:
      r.hour = static_cast<byte_t>(val % 24);
      break;
    case 0x0B:
      r.day = static_cast<std::uint16_t>((r.day & 0x100) | val);
      break;
    case 0x0C: {
      r.day = static_cast<std::uint16_t>(
          (r.day & 0x0FF) | ((val & 0x01) << 8)); // Hope the math is right...
      r.halt = (val & 0x40) != 0;
      r.carry = (val & 0x80) != 0;
      break;
    }
    default:
      break;
    }
  }

  void advance_rtc(std::uint32_t const seconds) {
    // RTC advancement
    std::uint32_t total = seconds;

    auto add = [&](byte_t &field, std::uint32_t const mod) {
      const std::uint32_t v = field + (total % mod);
      field = static_cast<byte_t>(v % mod);
      total = (total / mod) + (v / mod);
    };

    // seconds -> minutes -> hours -> days
    add(rtc_.sec, 60);
    add(rtc_.min, 60);
    add(rtc_.hour, 24);

    if (total > 0) {
      std::uint32_t day = (rtc_.day & 0x1FF) + total;
      if (day >= 512) {
        day %= 512;
        rtc_.carry = true; // set on overflow, stays until cleared by program
      }
      rtc_.day = static_cast<std::uint16_t>(day & 0x1FF);
    }
  }

  [[nodiscard]] byte_t ram_at(std::size_t const bank,
                              std::size_t const off) const {
    if (ram_.empty())
      return open_bus();
    const std::size_t banks =
        std::max<std::size_t>(1, ram_.size() / kRamBankSize);
    const std::size_t b = clamp_bank(bank, banks);
    const std::size_t idx = (b * kRamBankSize + off) % ram_.size();
    return ram_[idx];
  }

  void ram_write(std::size_t const bank, std::size_t const off,
                 byte_t const v) {
    if (ram_.empty())
      return;
    const std::size_t banks =
        std::max<std::size_t>(1, ram_.size() / kRamBankSize);
    const std::size_t b = clamp_bank(bank, banks);
    const std::size_t idx = (b * kRamBankSize + off) % ram_.size();
    ram_[idx] = v;
  }
};

std::unique_ptr<Mbc> make_mbc3(const cart &c) {
  const bool has_rtc =
      c.header.cartridge_type == 0x0F || // MBC3+TIMER+BATTERY
      c.header.cartridge_type == 0x10;   // MBC3+TIMER+RAM+BATTERY
  return std::make_unique<Mbc3>(c.rom_span(), c.declared_ram_bytes,
                                type_has_battery(c.header.cartridge_type),
                                has_rtc, c.special_mbc == MBC30_t);
}
