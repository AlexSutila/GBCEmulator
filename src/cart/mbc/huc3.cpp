#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"
#include "savestate/codec.hpp"

// ---------------------------
// HuC-3 (ROM + RAM + RTC/IR mailbox MCU)
// ---------------------------
// Pan Docs: https://gbdev.io/pandocs/HuC3.html
//
// Memory:
// Fixed ROM bank 00 (16 KiB)            (0000-3FFF)
// Switchable ROM bank 00-7F (16 KiB)    (4000-7FFF)   (bank 00 allowed)
// Switchable RAM bank 00-03 (8 KiB)     (A000-BFFF)   OR RTC/IR register
//
// Registers:
// RAM/RTC/IR select (low nibble)        (0000-1FFF)   [read/write in docs,
//                                                      writes are implemented]
//   0x0: Cart RAM (read-only)
//   0xA: Cart RAM (read/write)
//   0xB: RTC command/argument (write)
//   0xC: RTC command/response (read)
//   0xD: RTC semaphore (read/write)
//   0xE: IR (read/write)
// ROM bank select (7-bit)               (2000-3FFF)
// RAM bank select (>=2 bits)            (4000-5FFF)
// 6000-7FFF: unused
//
// Notes:
// - For the I/O registers: A12-A0 are not connected -> offset ignored
// - D7 is not connected -> reads MSB is open-bus-ish (usually high); writes
//   ignore D7

class HuC3 final : public Mbc {
public:
  HuC3(std::span<const byte_t> const rom, std::size_t const ram_bytes,
       bool const battery)
      : rom_(rom), ram_(ram_bytes), battery_(battery) {
    // Initialize time window to 0
    sync_time_to_mcu();
  }

  void tick(std::chrono::seconds const elapsed) override {
    // Advance in minutes; this is "good enough" for HuC-3 titles and matches
    // the minute/day counters
    const auto secs = static_cast<std::uint64_t>(elapsed.count());
    if (secs == 0)
      return;

    sec_acc_ += secs;
    const std::uint64_t minutes = sec_acc_ / 60;
    sec_acc_ %= 60;

    if (minutes == 0)
      return;

    advance_minutes(static_cast<std::uint32_t>(minutes));
    sync_time_to_mcu();
  }

  byte_t read(addr_t const addr) override {
    if (addr <= 0x3FFF)
      return rom_at(rom_, 0, addr);
    if (addr <= 0x7FFF)
      return rom_at(rom_, rom_bank_, addr - 0x4000);

    if (addr >= 0xA000 && addr <= 0xBFFF) {
      switch (sel_) {
      case 0x0: // RAM read-only
      case 0xA: // RAM read/write
        return ram_read(addr - 0xA000);

      case 0xC: // RTC response (read)
        return rtc_response_read();

      case 0xD: // semaphore (read)
        return rtc_semaphore_read();

      case 0xE: // IR (read)
        return ir_read();

      default:
        return open_bus();
      }
    }

    return open_bus();
  }

  void write(addr_t const addr, byte_t const val_in) override {
    if (addr <= 0x1FFF) {
      sel_ = static_cast<byte_t>(val_in & 0x0F);
      return;
    }
    if (addr <= 0x3FFF) {
      rom_bank_ = static_cast<byte_t>(val_in & 0x7F);
      return;
    }
    if (addr <= 0x5FFF) {
      ram_bank_ = static_cast<byte_t>(val_in & 0x03);
      return;
    }
    if (addr <= 0x7FFF) {
      // Observed as no-op
      return;
    }

    if (addr >= 0xA000 && addr <= 0xBFFF) {
      const auto v = static_cast<byte_t>(val_in & 0x7F); // D7 ignored on write

      switch (sel_) {
      case 0x0: // RAM read-only
        return;

      case 0xA: // RAM read/write
        ram_write(addr - 0xA000, v);
        return;

      case 0xB: // RTC command/argument (write)
        rtc_cmdarg_write(v);
        return;

      case 0xD: // RTC semaphore (write)
        rtc_semaphore_write(v);
        return;

      case 0xE: // IR (write)
        ir_write(v);
        return;

      default:
        return;
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
    t.chunk_header(version, Savestate::C_MBC_HUC3);
    t.field_generic(F_SEL, sel_);
    t.field_generic(F_ROM_BANK, rom_bank_);
    t.field_generic(F_RAM_BANK, ram_bank_);
    t.field_generic(F_IR_TX_ON, ir_tx_on_);
    t.field_generic(F_IR_LIGHT, ir_light_);
    t.field_bytes(F_MCU, {mcu_.data(), mcu_.size()});
    t.field_generic(F_MCU_ADDR, mcu_addr_);
    t.field_generic(F_LAST_CMD, last_cmd_);
    t.field_generic(F_LAST_ARG, last_arg_);
    t.field_generic(F_LAST_RES, last_res_);
    t.field_generic(F_MCU_READY, mcu_ready_);
    t.field_generic(F_MINUTE_OF_DAY, minute_of_day_);
    t.field_generic(F_DAY_COUNTER, day_counter_);
    t.field_generic(F_SEC_ACC, sec_acc_);
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
  void parse_savestate(Savestate::Checker &t) override {
    parse_savestate_impl(t);
  }

private:
  std::span<const byte_t> rom_;
  std::vector<byte_t> ram_;
  bool battery_{};

  byte_t sel_{0x0};
  byte_t rom_bank_{0}; // HuC-3 allows bank 0 in 4000-7FFF (like MBC5)
  byte_t ram_bank_{0};

  // IR
  bool ir_tx_on_{false};
  bool ir_light_{false}; // TODO: simulated IR environment

  // RTC MCU model (256 nybbles, low 4 bits used)
  std::array<byte_t, 256> mcu_{};
  byte_t mcu_addr_{0};

  byte_t last_cmd_{0};
  byte_t last_arg_{0};
  byte_t last_res_{0};

  bool mcu_ready_{true};

  // Simple time backing: minute-of-day [0..1439], day counter [0..4095]
  std::uint16_t minute_of_day_{0};
  std::uint16_t day_counter_{0};
  std::uint64_t sec_acc_{0};

  static constexpr std::size_t kMinOfDayBase = 0x10; // 0x10-0x12
  static constexpr std::size_t kDayBase = 0x13;      // 0x13-0x15
  static constexpr std::size_t kOutBase = 0x00;      // 0x00-0x06
  static constexpr std::size_t kEventMinBase = 0x58; // 0x58-0x5A
  static constexpr std::size_t kEventDayBase = 0x5B; // 0x5B-0x5D

  enum : std::uint16_t {
    F_SEL = 1,
    F_ROM_BANK,
    F_RAM_BANK,
    F_IR_TX_ON,
    F_IR_LIGHT,
    F_MCU,
    F_MCU_ADDR,
    F_LAST_CMD,
    F_LAST_ARG,
    F_LAST_RES,
    F_MCU_READY,
    F_MINUTE_OF_DAY,
    F_DAY_COUNTER,
    F_SEC_ACC,
  };

  [[nodiscard]] byte_t ram_read(std::size_t const off) const {
    if (ram_.empty())
      return open_bus();
    const std::size_t banks =
        std::max<std::size_t>(1, ram_.size() / kRamBankSize);
    const std::size_t b = clamp_bank(ram_bank_, banks);
    return ram_[(b * kRamBankSize + off) % ram_.size()];
  }

  void ram_write(std::size_t const off, byte_t const v) {
    if (ram_.empty())
      return;
    const std::size_t banks =
        std::max<std::size_t>(1, ram_.size() / kRamBankSize);
    const std::size_t b = clamp_bank(ram_bank_, banks);
    ram_[(b * kRamBankSize + off) % ram_.size()] = v;
  }

  // ---- nybble helpers ----
  static byte_t nyb(byte_t const v) { return static_cast<byte_t>(v & 0x0F); }

  [[nodiscard]] std::uint16_t read12(std::size_t const base) const {
    return static_cast<std::uint16_t>(nyb(mcu_[base + 0]) |
                                      (nyb(mcu_[base + 1]) << 4) |
                                      (nyb(mcu_[base + 2]) << 8));
  }

  void write12(std::size_t const base, std::uint16_t const v) {
    mcu_[base + 0] = static_cast<byte_t>(v & 0x0F);
    mcu_[base + 1] = static_cast<byte_t>((v >> 4) & 0x0F);
    mcu_[base + 2] = static_cast<byte_t>((v >> 8) & 0x0F);
  }

  void sync_time_from_mcu() {
    minute_of_day_ = static_cast<std::uint16_t>(read12(kMinOfDayBase) % 1440u);
    day_counter_ = static_cast<std::uint16_t>(read12(kDayBase) & 0x0FFFu);
  }

  void sync_time_to_mcu() {
    write12(kMinOfDayBase, minute_of_day_);
    write12(kDayBase, day_counter_);
  }

  void advance_minutes(std::uint32_t minutes) {
    while (minutes--) {
      minute_of_day_++;
      if (minute_of_day_ >= 1440) {
        minute_of_day_ = 0;
        day_counter_ = static_cast<std::uint16_t>((day_counter_ + 1) & 0x0FFF);
      }
    }
  }

  // ---- IR ----
  [[nodiscard]] byte_t ir_read() const {
    // D7 open bus (usually 1); docs describe C0/C1 like HuC1
    return static_cast<byte_t>(0xC0 | (ir_light_ ? 0x01 : 0x00));
  }

  void ir_write(byte_t const v) { ir_tx_on_ = (v & 0x01) != 0; }

  // ---- RTC mailbox protocol ----
  void rtc_cmdarg_write(byte_t const v) {
    last_cmd_ = static_cast<byte_t>((v >> 4) & 0x07); // bits 6-4
    last_arg_ = static_cast<byte_t>(v & 0x0F);        // bits 3-0
    // Writing does not execute; execution is requested via semaphore
  }

  [[nodiscard]] byte_t rtc_response_read() const {
    // Bits 6-4: last command, bits 3-0: result; bit7 open-bus-ish (1)
    return static_cast<byte_t>(0x80 | ((last_cmd_ & 0x07) << 4) |
                               (last_res_ & 0x0F));
  }

  [[nodiscard]] byte_t rtc_semaphore_read() const {
    // LSB=1 ready, 0 busy. Other bits read as open-bus-ish (usually 1)
    return static_cast<byte_t>(0xFE | (mcu_ready_ ? 0x01 : 0x00));
  }

  void rtc_semaphore_write(byte_t const v) {
    // Writing with LSB clear requests execution
    if ((v & 0x01) != 0)
      return;

    mcu_ready_ = false;
    execute_rtc_command();
    mcu_ready_ = true;
  }

  void execute_rtc_command() {
    last_res_ = 0;

    switch (last_cmd_) {
    case 0x1: { // Read nybble and increment address
      last_res_ = nyb(mcu_[mcu_addr_]);
      mcu_addr_ = static_cast<byte_t>(mcu_addr_ + 1);
      break;
    }
    case 0x3: { // Write nybble and increment address
      mcu_[mcu_addr_] = nyb(last_arg_);
      mcu_addr_ = static_cast<byte_t>(mcu_addr_ + 1);
      last_res_ = 0;
      break;
    }
    case 0x4: { // Set access address low nybble
      mcu_addr_ = static_cast<byte_t>((mcu_addr_ & 0xF0) | nyb(last_arg_));
      last_res_ = 0;
      break;
    }
    case 0x5: { // Set access address high nybble
      mcu_addr_ =
          static_cast<byte_t>((mcu_addr_ & 0x0F) | (nyb(last_arg_) << 4));
      last_res_ = 0;
      break;
    }
    case 0x6: { // Extended command
      exec_extended(last_arg_);
      break;
    }
    case 0x2: {
      // Observed in Pocket Family GB2, purpose unknown; return 0 by default
      last_res_ = 0;
      break;
    }
    default:
      last_res_ = 0;
      break;
    }
  }

  void exec_extended(byte_t const arg) {
    switch (arg & 0x0F) {
    case 0x0: // Copy current time to 0x00-0x06
      write12(kOutBase + 0, minute_of_day_);
      write12(kOutBase + 3, day_counter_);
      mcu_[kOutBase + 6] = 0;
      last_res_ = 0;
      break;

    case 0x1: { // Copy 0x00-0x06 to current time, update event time to keep
                // delta
      const std::uint32_t old_abs =
          static_cast<std::uint32_t>(day_counter_) * 1440u + minute_of_day_;

      const auto new_min =
          static_cast<std::uint16_t>(read12(kOutBase + 0) % 1440u);
      const auto new_day =
          static_cast<std::uint16_t>(read12(kOutBase + 3) & 0x0FFF);

      // Event time delta maintenance
      const auto ev_min = read12(kEventMinBase) % 1440u;
      const auto ev_day =
          static_cast<std::uint32_t>(read12(kEventDayBase) & 0x0FFF);
      const std::uint32_t ev_abs = ev_day * 1440u + ev_min;

      const std::int64_t delta = static_cast<std::int64_t>(ev_abs) -
                                 static_cast<std::int64_t>(old_abs);

      minute_of_day_ = new_min;
      day_counter_ = new_day;
      sync_time_to_mcu();

      const std::uint32_t new_abs =
          static_cast<std::uint32_t>(day_counter_) * 1440u + minute_of_day_;

      const std::int64_t new_ev_abs_s =
          static_cast<std::int64_t>(new_abs) + delta;
      const std::uint32_t new_ev_abs =
          (new_ev_abs_s < 0) ? 0u : static_cast<std::uint32_t>(new_ev_abs_s);

      const auto new_ev_day =
          static_cast<std::uint16_t>((new_ev_abs / 1440u) & 0x0FFF);
      const auto new_ev_min = static_cast<std::uint16_t>(new_ev_abs % 1440u);

      write12(kEventDayBase, new_ev_day);
      write12(kEventMinBase, new_ev_min);

      last_res_ = 0;
      break;
    }

    case 0x2:
      // games won't start if result is not 1.
      last_res_ = 0x1;
      break;

    case 0xE:
      // executing twice triggers tone generator; we just acknowledge.
    default:
      last_res_ = 0;
      break;
    }
  }
};

std::unique_ptr<Mbc> make_huc3(const cart &c) {
  return std::make_unique<HuC3>(c.rom_span(), c.declared_ram_bytes,
                                type_has_battery(c.header.cartridge_type));
}
