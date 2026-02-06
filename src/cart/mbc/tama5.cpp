#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

// ---------------------------
// TAMA5 (Bandai)
// ---------------------------
// GBDEV Reference: https://gbdev.gg8.se/forums/viewtopic.php?id=469
//
// Interface:
//   $A000: Register nybble (high 4 bits are hi-z -> read as 1s)
//   $A001: Register number
//
// Unlock:
//   Write $0A to $A001, then poll reads from $A000 until it returns $F1,
//   then normal register accesses work
//
// Registers (nybbles):
//   $0: ROM bank low  (RA14-17)
//   $1: ROM bank high (RA18)
//   $4: Data in low
//   $5: Data in high
//   $6: Address/command high
//   $7: Address/command low   (writing this triggers command execution)
//   $C: Data out low
//   $D: Data out high
//   $A: Constant 1? (also used during unlock polling)
//
// Command byte: CMD = ($6 << 4) | $7
//   - 00-1F: RAM read   (autistic)
//   - 20-3F: RAM write
//   - 40-6F: TAMA6 commands
//   - 70-7F: "open bus" echo (we place CMD into data out)
//   - 80-BF: RTC ops (RTC selected with $6:8; op in $7)
//
// RAM:
//   32 bytes, addressed by (addr_hi_bit << 4) | addr_lo_nybble
//   RAM write: set $4,$5 then write $6:$2|addrHi and $7:addrLo (exec)
//   RAM read : write $6:addrHi and $7:addrLo then read $C,$D
//
// RTC (TC8521AM):
//   - RTC register number is in $4
//   - RTC op/page/rw is in $7:
//       bit0: 0=write, 1=read
//       bits1-2: page (0=timer,1=alarm,2/3=free)
//   - RTC write value (nybble) is in $5
//   - Writing shared regs D/E/F directly is filtered out by TAMA6,
//     so direct writes to reg>=D are ignored here.
//   - PAGE reg bits (shared reg D):
//       bits0-1 page select
//       bit2 ALARM ENABLE
//       bit3 TIMER ENABLE
//
// TAMA6 commands (CMD in 0x40-0x6F):
//   Values passed as two-digit BCD: tens in $5, ones in $4
//   - 40: disable TIMER ENABLE
//   - 41: enable TIMER ENABLE and reset seconds to 00
//   - 43: observed unknown (noop)
//   - 44/45: atomically set minutes / hours
//   - 46/47: atomically get minutes / hours (uses TAMA6 cache)
//   - 50/51: disable/enable ALARM ENABLE
//
// Notes:
//   The post contains one apparent inversion between the “ranges” list and the
//   “procedure” description for RAM read/write. This implementation follows the
//   explicit procedure description: bit1 ($2) indicates RAM write

class Tama5 final : public Mbc {
public:
  Tama5(const std::span<const byte_t> rom, bool const battery)
      : rom_(rom), save_(kSaveBytes, 0xFF), battery_(battery) {}

  void tick(std::chrono::seconds const elapsed) override {
    // Only advance when TIMER ENABLE is set in PAGE register
    if ((rtc_page_reg_ & kPageTimerEnable) == 0)
      return;
    advance_rtc(static_cast<std::uint32_t>(elapsed.count()));
  }

  byte_t read(addr_t const addr) override {
    if (addr <= 0x3FFF)
      return rom_at(0, addr);
    if (addr <= 0x7FFF)
      return rom_at(rom_bank_, addr - 0x4000);

    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (!unlocked_ && !unlock_pending_) {
        // Pre-init repeating pattern (F0/FF) across Axxx/Bxxx
        pattern_flip_ = !pattern_flip_;
        return pattern_flip_ ? 0xF0 : 0xFF;
      }

      if (addr == 0xA000)
        return read_port();
      if (addr == 0xA001)
        return open_bus();
      return open_bus();
    }

    return open_bus();
  }

  void write(addr_t const addr, byte_t const val) override {
    // Some games poke low ROM areas on TAMA5 boards; behavior is unclear
    // We currently ignore 0000-7FFF writes

    if (addr >= 0xA000 && addr <= 0xBFFF) {
      if (addr == 0xA001) {
        reg_sel_ = static_cast<byte_t>(val & 0x0F);
        if (!unlocked_ && reg_sel_ == 0x0A)
          unlock_pending_ = true;
        return;
      }
      if (addr == 0xA000) {
        if (!unlocked_ && !unlock_pending_)
          return;
        write_port(static_cast<byte_t>(val & 0x0F));
        return;
      }
    }
  }

  [[nodiscard]] bool has_battery() const noexcept override { return battery_; }

  // Save layout:
  //   [0..31]   : 32-byte RAM
  //   [32..]    : RTC state (page0-3 regs0-C as nybbles + shared PAGE reg)
  [[nodiscard]] std::span<const byte_t> ram() const noexcept override {
    return save_;
  }
  std::span<byte_t> ram() noexcept override { return save_; }

private:
  std::span<const byte_t> rom_;
  std::vector<byte_t> save_;
  bool battery_{false};

  // ---------------------------
  // Persistent storage layout
  // ---------------------------
  static constexpr std::size_t kRamBytes = 32;
  static constexpr std::size_t kRtcPages = 4;
  static constexpr std::size_t kRtcRegsPerPage = 13; // 0..C
  static constexpr std::size_t kSaveBytes =
      kRamBytes + (kRtcPages * kRtcRegsPerPage) + 1; // + PAGE reg nybble

  byte_t &ram_byte_(std::size_t const i) { return save_[i % kRamBytes]; }
  [[nodiscard]] byte_t ram_byte_(std::size_t const i) const {
    return save_[i % kRamBytes];
  }

  static std::size_t rtc_base_() { return kRamBytes; }
  static std::size_t rtc_idx_(std::size_t const page,
                              std::size_t const reg) {
    return rtc_base_() + page * kRtcRegsPerPage + reg;
  }

  byte_t &rtc_nyb_(std::size_t const page, std::size_t const reg) {
    return save_[rtc_idx_(page, reg)];
  }
  [[nodiscard]] byte_t rtc_nyb_(std::size_t const page,
                                std::size_t const reg) const {
    return static_cast<byte_t>(save_[rtc_idx_(page, reg)] & 0x0F);
  }

  byte_t &rtc_page_reg_slot_() { return save_[kSaveBytes - 1]; }
  [[nodiscard]] byte_t rtc_page_reg_slot_() const {
    return static_cast<byte_t>(save_[kSaveBytes - 1] & 0x0F);
  }

  // ---------------------------
  // TAMA5 state
  // ---------------------------
  bool unlocked_{false};
  bool unlock_pending_{false};
  bool pattern_flip_{false};

  byte_t reg_sel_{0};
  byte_t regs_[0x10]{}; // nybble registers, masked on read/write

  std::size_t rom_bank_{1};

  // ---------------------------
  // RTC state (TC8521AM-ish)
  // ---------------------------
  static constexpr byte_t kPageSelectMask = 0x03;
  static constexpr byte_t kPageAlarmEnable = 0x04;
  static constexpr byte_t kPageTimerEnable = 0x08;

  // Shared PAGE register (RTC reg D)
  byte_t rtc_page_reg_{0x0};

  // TAMA6 "cache" for minutes/hours
  // Updated by tick() and TAMA6 set commands; NOT updated by direct RTC writes
  byte_t cached_min_{0};
  byte_t cached_hour_{0};

  // RTC register masks per page (only regs 0..C are stored per-page here)
  static constexpr byte_t kMaskTimer[kRtcRegsPerPage] = {
      0xF, // 0: 1-sec digit
      0x7, // 1: 10-sec digit
      0xF, // 2: 1-min digit
      0x7, // 3: 10-min digit
      0xF, // 4: 1-hour digit
      0x3, // 5: 10-hour digit (2-bit)
      0x7, // 6: day of week
      0xF, // 7: 1-day digit
      0x3, // 8: 10-day digit
      0xF, // 9: 1-month digit
      0x1, // A: 10-month digit
      0xF, // B: 1-year digit
      0xF  // C: 10-year digit
  };

  static constexpr byte_t kMaskAlarm[kRtcRegsPerPage] = {
      0x0, // 0
      0x0, // 1
      0xF, // 2: 1-min
      0x7, // 3: 10-min
      0xF, // 4: 1-hour
      0x3, // 5: 10-hour (2-bit)
      0x7, // 6: day of week
      0xF, // 7: 1-day
      0x3, // 8: 10-day
      0x0, // 9
      0x1, // A: 24-hour mode bit
      0x3, // B: leap-year bits (2-bit)
      0x0  // C
  };

  static constexpr byte_t kMaskFree[kRtcRegsPerPage] = {
      0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF};

  static constexpr byte_t mask_for_(std::size_t const page,
                                    std::size_t const reg) {
    if (reg >= kRtcRegsPerPage)
      return 0x0;
    if (page == 0)
      return kMaskTimer[reg];
    if (page == 1)
      return kMaskAlarm[reg];
    return kMaskFree[reg];
  }

  // ---------------------------
  // Port helpers ($A000/$A001)
  // ---------------------------
  byte_t read_port() {
    if (unlock_pending_) {
      // Return F1 (hi-z high bits -> 1s), then mark unlocked
      unlock_pending_ = false;
      unlocked_ = true;
      rtc_page_reg_ = rtc_page_reg_slot_() & 0x0F;
      return 0xF1;
    }

    const byte_t v = read_reg_nybble_(reg_sel_);
    return static_cast<byte_t>(0xF0 | (v & 0x0F));
  }

  void write_port(byte_t const nyb) {
    if (unlock_pending_) {
      // During the unlock poll window, ignore writes
      return;
    }
    write_reg_nybble_(reg_sel_, static_cast<byte_t>(nyb & 0x0F));
  }

  // ---------------------------
  // Register read/write (nybble)
  // ---------------------------
  [[nodiscard]] byte_t read_reg_nybble_(byte_t const r) const {
    const auto reg = static_cast<byte_t>(r & 0x0F);
    if (reg == 0x0A)
      return 0x01; // "constant 1" behavior
    if (reg == 0x0C || reg == 0x0D)
      return static_cast<byte_t>(regs_[reg] & 0x0F);

    return static_cast<byte_t>(regs_[reg] & 0x0F);
  }

  void write_reg_nybble_(byte_t const r, byte_t const v) {
    const auto reg = static_cast<byte_t>(r & 0x0F);
    const auto nyb = static_cast<byte_t>(v & 0x0F);

    if (reg == 0x0A) {
      // constant reg; ignore writes
      return;
    }
    regs_[reg] = nyb;
    // ROM bank updates
    if (reg == 0x00 || reg == 0x01) {
      update_rom_bank_();
      return;
    }
    // Writing address/command low triggers execution
    if (reg == 0x07) {
      exec_command_();
      return;
    }
    // Keep PAGE register persisted
    rtc_page_reg_slot_() = static_cast<byte_t>(rtc_page_reg_ & 0x0F);
  }

  void update_rom_bank_() {
    std::size_t b = ((regs_[0x01] & 0x01) << 4) | (regs_[0x00] & 0x0F);
    if (b == 0)
      b = 1;
    rom_bank_ = b;
  }

  [[nodiscard]] byte_t data_in_byte_() const {
    return static_cast<byte_t>(((regs_[0x05] & 0x0F) << 4) | (regs_[0x04] & 0x0F));
  }

  void set_data_out_byte_(byte_t const b) {
    regs_[0x0C] = static_cast<byte_t>(b & 0x0F);
    regs_[0x0D] = static_cast<byte_t>((b >> 4) & 0x0F);
  }

  // ---------------------------
  // Command execution
  // ---------------------------
  void exec_command_() {
    const auto cmd = static_cast<byte_t>(((regs_[0x06] & 0x0F) << 4) |
                                           (regs_[0x07] & 0x0F));

    // 0x70-0x7F: "open bus" echo (place cmd into data-out, maybe sufficient)
    if ((cmd & 0xF0) == 0x70) {
      set_data_out_byte_(cmd);
      return;
    }
    // RTC selected with $6:8 (0x8?)
    if ((regs_[0x06] & 0x0F) == 0x08) {
      rtc_op_();
      rtc_page_reg_slot_() = static_cast<byte_t>(rtc_page_reg_ & 0x0F);
      return;
    }
    // RAM read/write
    if (cmd <= 0x1F || (cmd >= 0x20 && cmd <= 0x3F)) {
      ram_op_();
      return;
    }
    // TAMA6 commands
    if (cmd >= 0x40 && cmd <= 0x6F) {
      tama6_op_(cmd);
      rtc_page_reg_slot_() = static_cast<byte_t>(rtc_page_reg_ & 0x0F);
      return;
    }
    // Untested / unknown ranges: noop
  }

  void ram_op_() {
    const bool is_write = (regs_[0x06] & 0x02) != 0; // per procedure in post
    const auto addr =
        static_cast<std::size_t>(((regs_[0x06] & 0x01) << 4) | (regs_[0x07] & 0x0F));
    if (is_write) {
      ram_byte_(addr) = data_in_byte_();
      return;
    }
    set_data_out_byte_(ram_byte_(addr));
  }

  void rtc_op_() {
    // $4: RTC reg number, $7: op/page, $5: value for write
    const auto regno = static_cast<byte_t>(regs_[0x04] & 0x0F);
    const auto op = static_cast<byte_t>(regs_[0x07] & 0x0F);
    const auto page = static_cast<std::size_t>((op >> 1) & 0x03);
    const bool is_read = (op & 0x01) != 0;
    const auto val = static_cast<byte_t>(regs_[0x05] & 0x0F);

    if (is_read) {
      const byte_t out = rtc_read_(page, regno);
      regs_[0x0C] = static_cast<byte_t>(out & 0x0F);
      regs_[0x0D] = 0x0;
      return;
    }
    // Direct writes to shared regs D/E/F are filtered out by TAMA6
    if (regno >= 0x0D)
      return;

    rtc_write_(page, regno, val, /*allow_shared=*/false);
  }

  void tama6_op_(byte_t const cmd) {
    const auto ones = static_cast<byte_t>(regs_[0x04] & 0x0F);
    const auto tens = static_cast<byte_t>(regs_[0x05] & 0x0F);
    const auto bcd = static_cast<byte_t>(tens * 10 + ones);

    auto set_minutes = [&](byte_t const m) {
      const auto mm = static_cast<byte_t>(m % 60);
      // Timer page (page0): regs2/3 are minute ones/tens
      rtc_write_(0, 0x02, static_cast<byte_t>(mm % 10), true);
      rtc_write_(0, 0x03, static_cast<byte_t>(mm / 10), true);
      cached_min_ = mm;
    };

    auto set_hours = [&](byte_t const h) {
      const auto hh = static_cast<byte_t>(h % 24);
      // Timer page (page0): regs4/5 are hour ones/tens
      rtc_write_(0, 0x04, static_cast<byte_t>(hh % 10), true);
      rtc_write_(0, 0x05, static_cast<byte_t>(hh / 10), true);
      cached_hour_ = hh;
    };

    auto get_minutes = [&] {
      const auto mm = static_cast<byte_t>(cached_min_ % 60);
      regs_[0x0C] = static_cast<byte_t>(mm % 10); // ones
      regs_[0x0D] = static_cast<byte_t>(mm / 10); // tens
    };

    auto get_hours = [&] {
      const auto hh = static_cast<byte_t>(cached_hour_ % 24);
      regs_[0x0C] = static_cast<byte_t>(hh % 10); // ones
      regs_[0x0D] = static_cast<byte_t>(hh / 10); // tens
    };

    switch (cmd) {
    case 0x40:
      rtc_page_reg_ = static_cast<byte_t>(rtc_page_reg_ & ~kPageTimerEnable);
      break;
    case 0x41:
      rtc_page_reg_ = static_cast<byte_t>(rtc_page_reg_ | kPageTimerEnable);
      // reset seconds to 00
      rtc_write_(0, 0x00, 0, true);
      rtc_write_(0, 0x01, 0, true);
      break;
    case 0x43:
      // Unknown behavior observed; noop for now
      break;
    case 0x44:
      set_minutes(bcd);
      break;
    case 0x45:
      set_hours(bcd);
      break;
    case 0x46:
      get_minutes();
      break;
    case 0x47:
      get_hours();
      break;
    case 0x50:
      rtc_page_reg_ = static_cast<byte_t>(rtc_page_reg_ & ~kPageAlarmEnable);
      break;
    case 0x51:
      rtc_page_reg_ = static_cast<byte_t>(rtc_page_reg_ | kPageAlarmEnable);
      break;
    default:
      break;
    }
  }

  // ---------------------------
  // RTC read/write helpers
  // ---------------------------
  [[nodiscard]] byte_t rtc_read_(std::size_t const page,
                                byte_t const regno) const {
    const auto r = static_cast<byte_t>(regno & 0x0F);

    if (r <= 0x0C) {
      const byte_t v = rtc_nyb_(page, r);
      const byte_t m = mask_for_(page, r);
      return static_cast<byte_t>(v & m);
    }

    if (r == 0x0D) {
      return static_cast<byte_t>(rtc_page_reg_ & 0x0F);
    }

    // TEST (E) and RESET (F) read as 0
    return 0x0;
  }

  void rtc_write_(std::size_t const page, byte_t const regno, byte_t const v,
                  bool const allow_shared) {
    const auto r = static_cast<byte_t>(regno & 0x0F);
    const auto nyb = static_cast<byte_t>(v & 0x0F);

    if (r <= 0x0C) {
      const byte_t m = mask_for_(page, r);
      rtc_nyb_(page, r) = static_cast<byte_t>(nyb & m);
      return;
    }
    if (!allow_shared)
      return;
    if (r == 0x0D) {
      rtc_page_reg_ = static_cast<byte_t>(nyb & 0x0F);
      return;
    }
    if (r == 0x0F) {
      // RESET register (write-only). Bits described in TC8521AM docs
      // Bit0: reset alarm, Bit1: reset timer, Bit2/3: ALARM freq selects
      if (nyb & 0x01) {
        // Reset alarm page registers (best-effort: clear the meaningful fields)
        for (std::size_t i = 0; i < kRtcRegsPerPage; ++i)
          rtc_nyb_(1, i) = static_cast<byte_t>(rtc_nyb_(1, i) & mask_for_(1, i));
      }
      if (nyb & 0x02) {
        // Reset timer: clear seconds/minutes/hours to 00:00:00
        rtc_write_(0, 0x00, 0, true);
        rtc_write_(0, 0x01, 0, true);
        rtc_write_(0, 0x02, 0, true);
        rtc_write_(0, 0x03, 0, true);
        rtc_write_(0, 0x04, 0, true);
        rtc_write_(0, 0x05, 0, true);
        cached_min_ = 0;
        cached_hour_ = 0;
      }
    }
  }

  // ---------------------------
  // RTC ticking (calendar)
  // ---------------------------
  static int days_in_month(int const year, int const month, int const leap_mod4) {
    (void)year;
    const bool leap = (leap_mod4 == 0);
    switch (month) {
    case 1:  return 31;
    case 2:  return leap ? 29 : 28;
    case 3:  return 31;
    case 4:  return 30;
    case 5:  return 31;
    case 6:  return 30;
    case 7:  return 31;
    case 8:  return 31;
    case 9:  return 30;
    case 10: return 31;
    case 11: return 30;
    case 12: return 31;
    default: return 30;
    }
  }

  void decode_time_(int &sec, int &min, int &hour, int &dow, int &day,
                    int &month, int &year) const {
    const int s1 = rtc_nyb_(0, 0x00) & 0xF;
    const int s10 = rtc_nyb_(0, 0x01) & 0x7;
    sec = s10 * 10 + s1;

    const int m1 = rtc_nyb_(0, 0x02) & 0xF;
    const int m10 = rtc_nyb_(0, 0x03) & 0x7;
    min = m10 * 10 + m1;

    const bool mode24 = (rtc_nyb_(1, 0x0A) & 0x1) != 0;
    const int h1 = rtc_nyb_(0, 0x04) & 0xF;
    const int h10 = rtc_nyb_(0, 0x05) & 0x3;

    if (mode24) {
      hour = h10 * 10 + h1;
    } else {
      // 12h: bit1 of h10 is PM, bit0 is tens digit (0/1)
      const bool pm = (h10 & 0x2) != 0;
      const int tens = (h10 & 0x1);
      if (const int h12 = tens * 10 + h1; h12 == 12)
        hour = pm ? 12 : 0;
      else
        hour = pm ? (h12 + 12) : h12;
    }

    dow = rtc_nyb_(0, 0x06) & 0x7;

    const int d1 = rtc_nyb_(0, 0x07) & 0xF;
    const int d10 = rtc_nyb_(0, 0x08) & 0x3;
    day = d10 * 10 + d1;

    const int mo1 = rtc_nyb_(0, 0x09) & 0xF;
    const int mo10 = rtc_nyb_(0, 0x0A) & 0x1;
    month = mo10 * 10 + mo1;

    const int y1 = rtc_nyb_(0, 0x0B) & 0xF;
    const int y10 = rtc_nyb_(0, 0x0C) & 0xF;
    year = y10 * 10 + y1;
  }

  void encode_time_(int sec, int min, int hour, int dow, int day,
                    int month, int year) {
    sec %= 60;
    min %= 60;
    hour %= 24;
    dow %= 7;
    if (dow < 0) dow += 7;

    year %= 100;
    if (year < 0) year += 100;
    if (month < 1) month = 1;
    if (month > 12) month = 12;

    const bool mode24 = (rtc_nyb_(1, 0x0A) & 0x1) != 0;

    rtc_write_(0, 0x00, static_cast<byte_t>(sec % 10), true);
    rtc_write_(0, 0x01, static_cast<byte_t>(sec / 10), true);
    rtc_write_(0, 0x02, static_cast<byte_t>(min % 10), true);
    rtc_write_(0, 0x03, static_cast<byte_t>(min / 10), true);

    if (mode24) {
      rtc_write_(0, 0x04, static_cast<byte_t>(hour % 10), true);
      rtc_write_(0, 0x05, static_cast<byte_t>(hour / 10), true);
    } else {
      const bool pm = hour >= 12;
      int h12 = hour % 12;
      if (h12 == 0) h12 = 12;
      const int tens = h12 / 10; // 0 or 1
      const int ones = h12 % 10;
      const auto h10 = static_cast<byte_t>((tens & 0x1) | (pm ? 0x2 : 0x0));
      rtc_write_(0, 0x04, static_cast<byte_t>(ones), true);
      rtc_write_(0, 0x05, h10, true);
    }

    rtc_write_(0, 0x06, static_cast<byte_t>(dow), true);

    if (day < 1) day = 1;
    const int d10 = day / 10;
    const int d1 = day % 10;
    rtc_write_(0, 0x07, static_cast<byte_t>(d1), true);
    rtc_write_(0, 0x08, static_cast<byte_t>(d10), true);

    const int mo10 = month / 10;
    const int mo1 = month % 10;
    rtc_write_(0, 0x09, static_cast<byte_t>(mo1), true);
    rtc_write_(0, 0x0A, static_cast<byte_t>(mo10), true);

    const int y10 = year / 10;
    const int y1 = year % 10;
    rtc_write_(0, 0x0B, static_cast<byte_t>(y1), true);
    rtc_write_(0, 0x0C, static_cast<byte_t>(y10), true);

    cached_min_ = static_cast<byte_t>(min);
    cached_hour_ = static_cast<byte_t>(hour);
  }

  void advance_rtc(std::uint32_t const seconds) {
    int sec = 0, min = 0, hour = 0, dow = 0, day = 1, month = 1, year = 0;
    decode_time_(sec, min, hour, dow, day, month, year);

    // Leap-year mod-4 bits live in alarm page reg B (2-bit)
    int leap_mod4 = rtc_nyb_(1, 0x0B) & 0x3;

    std::uint32_t total = seconds;
    sec += static_cast<int>(total % 60);
    total /= 60;
    if (sec >= 60) { sec -= 60; total += 1; }

    min += static_cast<int>(total % 60);
    total /= 60;
    if (min >= 60) { min -= 60; total += 1; }

    hour += static_cast<int>(total % 24);
    total /= 24;
    if (hour >= 24) { hour -= 24; total += 1; }

    // Add remaining days with basic calendar handling
    while (total > 0) {
      const int dim = days_in_month(year, month, leap_mod4);
      ++day;
      dow = (dow + 1) % 7;

      if (day > dim) {
        day = 1;
        ++month;
        if (month > 12) {
          month = 1;
          ++year;
          if (year >= 100) year = 0;

          // update leap-year mod-4 counter
          leap_mod4 = (leap_mod4 + 1) & 0x3;
          rtc_write_(1, 0x0B, static_cast<byte_t>(leap_mod4), true);
        }
      }
      --total;
    }

    encode_time_(sec, min, hour, dow, day, month, year);
  }

  // ---------------------------
  // ROM helpers
  // ---------------------------
  [[nodiscard]] byte_t rom_at(std::size_t const bank,
                              std::size_t const off) const {
    const auto banks = rom_bank_count(rom_);
    const auto b = clamp_bank(bank, banks);
    const std::size_t idx = b * kRomBankSize + off;
    return (idx < rom_.size()) ? rom_[idx] : open_bus();
  }
};

std::unique_ptr<Mbc> make_tama5(const cart &c) {
  return std::make_unique<Tama5>(c.rom_span(), type_has_battery(c.header.cartridge_type));
}