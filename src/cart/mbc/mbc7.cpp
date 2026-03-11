#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "savestate/codec.hpp"

// ---------------------------
// MBC7 (Tilt sensor + EEPROM)
// ---------------------------
// Pan Docs: https://gbdev.io/pandocs/MBC7.html
// Microchip specs:
// https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/21712C.pdf
//
// Hardware:
// - 2-axis accelerometer (ADXL202E)
// - 256-byte EEPROM (93LC56)
//
// Memory:
// Fixed ROM bank 00 (16 KiB)             (0000-3FFF)
// Switchable ROM bank 00-7F (16 KiB)     (4000-7FFF)
// RAM registers (4 KiB, not SRAM)        (A000-AFFF)
// Unknown/unused (reads FF)              (B000-BFFF)
//
// Registers:
// RAM registers (read/write)             (A000-AFFF)
// RAM enable 1 (0A=enable, 00=disable)   (0000-1FFF)
// ROM bank number (write only)           (2000-3FFF)
// RAM enable 2 (40=enable)               (4000-5FFF)
//
// Notes:
// - A000-BFFF is NOT RAM; it's a register file. Registers are selected by addr
// bits 4-7.
// - Registers must be enabled by BOTH:
//    - write 0x0A (low nibble == A) to 0000-1FFF
//    - write 0x40 to 4000-5FFF
// - EEPROM is controlled through Ax8x "pins" (CS/CLK/DI) and read back via DO
// - EEPROM commands listed in Pan Docs are implemented; busy is modeled by DO=0
//   for a short number of clock edges after a programming op

// ---------------------------
// 93LC56-like EEPROM core (x16 mode, 128 words = 256 bytes)
// ---------------------------
class Eeprom93LC56 {
public:
  static constexpr std::size_t kBytes = 256;
  static constexpr std::size_t kWords = 128;

  Eeprom93LC56() { data_.fill(0xFF); }

  // Expose raw bytes for save persistence
  [[nodiscard]] std::span<const byte_t> bytes() const noexcept { return data_; }
  [[nodiscard]] std::span<byte_t> bytes() noexcept { return data_; }

  // Ax8x readback: expose pins (only DO is really needed, but returning the
  // full latched pin state makes debugging easier)
  [[nodiscard]] byte_t read_pins() const noexcept {
    byte_t v = 0;
    if (cs_)
      v |= 0x80;
    if (clk_)
      v |= 0x40;
    if (di_)
      v |= 0x02;
    if (do_)
      v |= 0x01;
    return v;
  }

  // Ax8x write: update pins and advance the serial protocol on CLK rising edges
  void write_pins(byte_t const v) {
    const bool new_cs = (v & 0x80) != 0;
    const bool new_clk = (v & 0x40) != 0;
    const bool new_di = (v & 0x02) != 0;

    const bool cs_fall = (cs_ && !new_cs);
    const bool clk_rise = (!clk_ && new_clk);

    cs_ = new_cs;
    clk_ = new_clk;
    di_ = new_di;

    if (!cs_) {
      // When CS is low, DO is high-Z on real hardware; for our register
      // readback, treating it as "ready/high" is sufficient A high-Z/high
      // impedance state means an electronic outputs neither 0 not 1,
      // effectively disconnecting it from the circuit
      do_ = true;
      // Require CS low between instructions (per MC docs); reset instruction
      // state
      reset_instruction_state();
      return;
    }
    if (cs_fall) {
      do_ = true;
      reset_instruction_state();
      return;
    }
    if (!clk_rise)
      return;

    // Busy handling: DO low while programming, then high when done
    if (busy_edges_ > 0) {
      --busy_edges_;
      do_ = (busy_edges_ == 0);
      if (busy_edges_ == 0) {
        // after busy completes, go idle and await next instruction
        reset_instruction_state();
      }
      return;
    }

    // READ output shifting (MSB-first)
    if (out_bits_ > 0) {
      do_ = ((out_shift_ & 0x8000) != 0);
      out_shift_ <<= 1;
      --out_bits_;
      if (out_bits_ == 0) {
        // After shifting all bits, DO would go high-Z; treat as high
        do_ = true;
      }
      return;
    }

    // If we're collecting write data bits
    if (mode_ == Mode::WriteWord || mode_ == Mode::WriteAll) {
      in_shift_ = static_cast<std::uint16_t>((in_shift_ << 1) | (di_ ? 1u : 0u));
      ++in_bits_;
      if (in_bits_ == 16) {
        if (ew_enabled_) {
          if (mode_ == Mode::WriteWord) {
            write_word(addr_, in_shift_);
          } else { // WRAL
            for (std::uint16_t a = 0; a < kWords; ++a)
              write_word(static_cast<std::uint8_t>(a), in_shift_);
          }
          start_busy();
        } else { // Writes ignored if EWEN not set
          do_ = true;
          reset_instruction_state();
        }
      }
      return;
    }

    // Instruction decoding:
    // Pan Docs: commands are preceded by a "1" start bit; games often send a
    // leading 0
    if (!start_seen_) {
      if (di_) {
        start_seen_ = true;
        cmd_ = 0;
        cmd_bits_ = 0;
      }
      return;
    }

    // After start bit, capture 10 command bits (MSB-first)
    cmd_ = static_cast<std::uint16_t>((cmd_ << 1) | (di_ ? 1u : 0u));
    ++cmd_bits_;
    if (cmd_bits_ == 10) {
      decode_command(cmd_);
      // For the next instruction, we need a new start bit
      start_seen_ = false;
      cmd_bits_ = 0;
      cmd_ = 0;
    }
  }

  template <typename T> void parse_savestate(T &t) {
    t.field_generic(F_CS, cs_);
    t.field_generic(F_CLK, clk_);
    t.field_generic(F_DI, di_);
    t.field_generic(F_DO, do_);
    t.field_generic(F_START_SEEN, start_seen_);
    t.field_generic(F_CMD, cmd_);
    t.field_generic(F_CMD_BITS, cmd_bits_);
    t.field_generic(F_OUT_SHIFT, out_shift_);
    t.field_generic(F_OUT_BITS, out_bits_);
    t.field_enum(F_MODE, mode_);
    t.field_generic(F_IN_SHIFT, in_shift_);
    t.field_generic(F_IN_BITS, in_bits_);
    t.field_generic(F_ADDR, addr_);
    t.field_generic(F_EW_ENABLED, ew_enabled_);
    t.field_generic(F_BUSY_EDGES, busy_edges_);
    t.eof();
  }

private:
  enum : std::uint16_t {
    F_CS = 1,
    F_CLK,
    F_DI,
    F_DO,
    F_START_SEEN,
    F_CMD,
    F_CMD_BITS,
    F_OUT_SHIFT,
    F_OUT_BITS,
    F_MODE,
    F_IN_SHIFT,
    F_IN_BITS,
    F_ADDR,
    F_EW_ENABLED,
    F_BUSY_EDGES,
  };

  // EEPROM memory layout:
  // Word address N corresponds to bytes [2N] (low) and [2N+1] (high)
  [[nodiscard]] std::uint16_t read_word(std::uint8_t const a) const noexcept {
    const std::size_t i = (static_cast<std::size_t>(a) & 0x7F) * 2;
    const std::uint16_t lo = data_[i];
    const std::uint16_t hi = data_[i + 1];
    return static_cast<std::uint16_t>(lo | (hi << 8));
  }

  void write_word(std::uint8_t const a, std::uint16_t const w) noexcept {
    const std::size_t i = (static_cast<std::size_t>(a) & 0x7F) * 2;
    data_[i] = static_cast<byte_t>(w & 0xFF);
    data_[i + 1] = static_cast<byte_t>((w >> 8) & 0xFF);
  }

  enum class Mode { Idle, WriteWord, WriteAll };

  void reset_instruction_state() noexcept {
    start_seen_ = false;
    cmd_ = 0;
    cmd_bits_ = 0;

    mode_ = Mode::Idle;
    in_shift_ = 0;
    in_bits_ = 0;

    out_shift_ = 0;
    out_bits_ = 0;

    addr_ = 0;
  }

  void start_busy() noexcept {
    // Real devices take milliseconds; games poll DO while clocking
    // This "edge-count" busy model is good enough for typical polling loops
    busy_edges_ = 64;
    do_ = false;
    reset_instruction_state(); // instruction is "done", now busy
  }

  void start_read(std::uint8_t const a) noexcept {
    addr_ = static_cast<std::uint8_t>(a & 0x7F);
    out_shift_ = read_word(addr_);
    out_bits_ = 16;
    // DO updates on the next clock edge; keep it high until then
    do_ = true;
  }

  void do_erase(std::uint8_t const a) noexcept {
    if (!ew_enabled_) {
      do_ = true;
      reset_instruction_state();
      return;
    }
    addr_ = static_cast<std::uint8_t>(a & 0x7F);
    write_word(addr_, 0xFFFF);
    start_busy();
  }

  void do_eral() noexcept {
    if (!ew_enabled_) {
      do_ = true;
      reset_instruction_state();
      return;
    }
    for (std::uint16_t a = 0; a < kWords; ++a)
      write_word(static_cast<std::uint8_t>(a), 0xFFFF);
    start_busy();
  }

  // Command is 10 bits following the start bit:
  // READ : 10xAAAAAAA
  // WRITE: 01xAAAAAAA (+16 data bits)
  // ERASE: 11xAAAAAAA
  // EWEN : 0011xxxxxx                      (Erase/Write Enable)
  // EWDS : 0000xxxxxx                      (Erase/Write Disable)
  // ERAL : 0010xxxxxx                      (Erase All)
  // WRAL : 0001xxxxxx (+16 data bits)      (Write All)
  void decode_command(std::uint16_t const cmd10) {
    const auto top4 = static_cast<std::uint16_t>((cmd10 >> 6) & 0x0F);
    const auto addr = static_cast<std::uint8_t>(cmd10 & 0x7F);
    const auto top2 = static_cast<std::uint16_t>((cmd10 >> 8) & 0x03);

    // Special commands have top2 == 00 and are differentiated by top4
    if (top2 == 0b00) {
      switch (top4) {
      case 0b0011: // EWEN
        ew_enabled_ = true;
        do_ = true;
        reset_instruction_state();
        return;
      case 0b0000: // EWDS
        ew_enabled_ = false;
        do_ = true;
        reset_instruction_state();
        return;
      case 0b0010: // ERAL
        do_eral();
        return;
      case 0b0001: // WRAL (+16 bits)
        if (!ew_enabled_) {
          do_ = true;
          reset_instruction_state();
          return;
        }
        mode_ = Mode::WriteAll;
        in_shift_ = 0;
        in_bits_ = 0;
        do_ = true;
        return;
      default:
        // Unknown "00xx" command; ignore
        do_ = true;
        reset_instruction_state();
        return;
      }
    }

    // Normal commands:
    switch (top2) {
    case 0b10: // READ
      start_read(addr);
      return;
    case 0b01: // WRITE (+16 bits)
      if (!ew_enabled_) {
        do_ = true;
        reset_instruction_state();
        return;
      }
      addr_ = static_cast<std::uint8_t>(addr & 0x7F);
      mode_ = Mode::WriteWord;
      in_shift_ = 0;
      in_bits_ = 0;
      do_ = true;
      return;
    case 0b11: // ERASE
      do_erase(addr);
      return;
    default:
      do_ = true;
      reset_instruction_state();
      return;
    }
  }

  std::array<byte_t, kBytes> data_{};

  // Pins
  bool cs_{false};
  bool clk_{false};
  bool di_{false};
  bool do_{true};

  // Instruction state
  bool start_seen_{false};
  std::uint16_t cmd_{0};
  int cmd_bits_{0};

  // Output shifting (READ)
  std::uint16_t out_shift_{0};
  int out_bits_{0};

  // Input shifting (WRITE/WRAL)
  Mode mode_{Mode::Idle};
  std::uint16_t in_shift_{0};
  int in_bits_{0};
  std::uint8_t addr_{0};

  bool ew_enabled_{false};

  // Busy modeled as a number of CLK rising edges while CS=1
  int busy_edges_{0};
};

class Mbc7 final : public Mbc {
public:
  explicit Mbc7(const std::span<const byte_t> rom, bool const battery)
      : rom_(rom), battery_(battery) {}

  byte_t read(addr_t const addr) override {
    if (addr <= 0x3FFF)
      return rom_at(rom_, 0, addr);

    if (addr <= 0x7FFF)
      return rom_at(rom_, rom_bank_, addr - 0x4000);

    if (addr >= 0xA000 && addr <= 0xAFFF) {
      if (!regs_enabled())
        return 0xFF;

      switch (static_cast<byte_t>((addr >> 4) & 0x0F)) {
      case 0x0: // latch write-only
      case 0x1:
        return 0xFF;

      case 0x2: // X low
        return static_cast<byte_t>(latched_x_ & 0xFF);
      case 0x3: // X high
        return static_cast<byte_t>((latched_x_ >> 8) & 0xFF);

      case 0x4: // Y low
        return static_cast<byte_t>(latched_y_ & 0xFF);
      case 0x5: // Y high
        return static_cast<byte_t>((latched_y_ >> 8) & 0xFF);

      case 0x6:
        return 0x00;
      case 0x7:
        return 0xFF;

      case 0x8: // EEPROM pins
        return eeprom_.read_pins();

      default:
        return 0xFF;
      }
    }

    if (addr >= 0xB000 && addr <= 0xBFFF) {
      // Pan Docs: reads as FF / unknown
      return 0xFF;
    }

    return open_bus();
  }

  void write(addr_t const addr, byte_t const val) override {
    if (addr <= 0x1FFF) {
      // RAM enable 1: value 0x0A enables
      ram_en1_ = ((val & 0x0F) == 0x0A);
      return;
    }

    if (addr <= 0x3FFF) {
      // ROM bank number (00-7F); bank 0 mapping needs confirmation, but many
      // emulators allow 0 here
      rom_bank_ = static_cast<byte_t>(val & 0x7F);
      return;
    }

    if (addr <= 0x5FFF) {
      // RAM enable 2: writing 0x40 enables register access
      ram_en2_ = (val == 0x40);
      return;
    }

    if (addr >= 0xA000 && addr <= 0xAFFF) {
      if (!regs_enabled())
        return;

      switch (static_cast<byte_t>((addr >> 4) & 0x0F)) {
      case 0x0:
        // Write 0x55 to erase latched data (reset to 0x8000)
        if (val == 0x55) {
          latched_x_ = 0x8000;
          latched_y_ = 0x8000;
          needs_erase_before_latch_ = false;
        }
        return;

      case 0x1:
        // Write 0xAA to latch; cannot re-latch until erased again
        if (val == 0xAA && !needs_erase_before_latch_) {
          latched_x_ = accel_x_;
          latched_y_ = accel_y_;
          needs_erase_before_latch_ = true;
        }
        return;

      case 0x8:
        // EEPROM pins (CS/CLK/DI); DO is read back
        eeprom_.write_pins(val);
        return;

      default:
        // Other registers are read-only or unused
        return;
      }
    }

    // other ranges ignored
  }

  // Treat the EEPROM as the "RAM" blob for save persistence
  [[nodiscard]] std::span<const byte_t> ram() const noexcept override { return eeprom_.bytes(); }
  std::span<byte_t> ram() noexcept override { return eeprom_.bytes(); }

  // Optional accelerometer hook that can be called from frontend/input later:
  // (Pan Docs says centered around 0x81D0; 0x8000 is the "unlatched" reset
  // value)
  void set_accel_raw(std::uint16_t const x, std::uint16_t const y) noexcept {
    accel_x_ = x;
    accel_y_ = y;
  }

  template <typename T> void parse_savestate_impl(T &t) {
    constexpr auto version = 1; // Schema revision
    t.chunk_header(version, Savestate::C_MBC_7);
    t.field_generic(F_ROM_BANK, rom_bank_);
    t.field_generic(F_RAM_EN1, ram_en1_);
    t.field_generic(F_RAM_EN2, ram_en2_);
    t.field_generic(F_ACCEL_X, accel_x_);
    t.field_generic(F_ACCEL_Y, accel_y_);
    t.field_generic(F_LATCHED_X, latched_x_);
    t.field_generic(F_LATCHED_Y, latched_y_);
    t.field_generic(F_NEEDS_ERASE_BEFORE_LATCH, needs_erase_before_latch_);
    t.field_complex(F_EEPROM_STATE, [&](T &t) { eeprom_.parse_savestate(t); });
    t.eof();
  }

  void parse_savestate(Savestate::Writer &t) override { parse_savestate_impl(t); }
  void parse_savestate(Savestate::Reader &t) override { parse_savestate_impl(t); }
  void parse_savestate(Savestate::Sizer &t) override { parse_savestate_impl(t); }
  void parse_savestate(Savestate::Checker &t) override { parse_savestate_impl(t); }

private:
  [[nodiscard]] bool has_battery() const noexcept override { return battery_; }
  [[nodiscard]] bool regs_enabled() const noexcept { return ram_en1_ && ram_en2_; }

  std::span<const byte_t> rom_;
  bool battery_{false};

  byte_t rom_bank_{1};

  bool ram_en1_{false};
  bool ram_en2_{false};

  // Accelerometer
  std::uint16_t accel_x_{0x81D0};
  std::uint16_t accel_y_{0x81D0};

  std::uint16_t latched_x_{0x8000};
  std::uint16_t latched_y_{0x8000};
  bool needs_erase_before_latch_{false};

  enum : std::uint16_t {
    F_ROM_BANK = 1,
    F_RAM_EN1,
    F_RAM_EN2,
    F_ACCEL_X,
    F_ACCEL_Y,
    F_LATCHED_X,
    F_LATCHED_Y,
    F_NEEDS_ERASE_BEFORE_LATCH,
    F_EEPROM_STATE,
  };

  Eeprom93LC56 eeprom_{};
};

std::unique_ptr<Mbc> make_mbc7(cart const &c) {
  return std::make_unique<Mbc7>(c.rom, type_has_battery(c.header.cartridge_type));
}
