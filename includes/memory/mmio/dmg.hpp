#ifndef __MMIO_DMG_H
#define __MMIO_DMG_H

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"

namespace PPU {

/*
 * FF41 — STAT: LCD Status Register
 *
 * Bit 7 — LYC interrupt select (R/W)
 *   If set, a STAT interrupt is requested when LY == LYC.
 *
 * Bit 6 — Mode 2 interrupt select (R/W)
 *   If set, a STAT interrupt is requested when the PPU enters Mode 2 (OAM
 * search).
 *
 * Bit 5 — Mode 1 interrupt select (R/W)
 *   If set, a STAT interrupt is requested when the PPU enters Mode 1 (V-Blank).
 *
 * Bit 4 — Mode 0 interrupt select (R/W)
 *   If set, a STAT interrupt is requested when the PPU enters Mode 0 (H-Blank).
 *
 * Bit 3 — LY == LYC flag (R)
 *   Set when the current scanline (LY) equals LYC.
 *   This bit is updated continuously by hardware.
 *
 * Bits 1–0 — PPU mode (R)
 *   Indicates the current PPU mode:
 *     0: H-Blank
 *     1: V-Blank
 *     2: OAM search
 *     3: Pixel transfer
 *   Reads as 0 when the PPU is disabled.
 */

enum class StatIntFlags : byte_t {
  LYC_EQ_LY = 1 << 2,
  MODE_0_SEL = 1 << 3,
  MODE_1_SEL = 1 << 4,
  MODE_2_SEL = 1 << 5,
  LYC_SEL = 1 << 6,
};

enum class StatModes : byte_t {
  MODE_HBLANK = 0,
  MODE_VBLANK = 1,
  MODE_OAM_SCAN = 2,
  MODE_DRAWING = 3,
};

class STAT : public MMIORegister {
public:
  void write(byte_t value) override;
  byte_t read() override;
  STAT() : state(0) {}

  /* PPU needs to check these flags to generate interrupts, but does not set
   * them itself afaik. Hence, we don't need a setter. */
  const bool int_enabled(StatIntFlags flag) const {
    return (state & static_cast<byte_t>(flag)) != 0;
  }
  const StatModes get_mode() const;
  void set_mode(StatModes mode);

private:
  byte_t state{};
};

/*
 * FF44 — LY: LCD Y Coordinate Register (Read-only)
 *
 * Indicates the current scanline being processed by the PPU.
 * The value may refer to a line that is about to be drawn,
 * currently being drawn, or has just been drawn.
 *
 * Valid range: 0–153
 *   0–143  : Visible scanlines
 *   144–153: V-Blank period
 */

class LY : public MMIORegister {
public:
  void write(byte_t) override;
  byte_t read() override;
  LY() : state(0) {}

  const bool is_visible() const { return state <= 143; }
  const bool is_vblank() const { return state >= 144; }

  void reset() { state = 0; };
  bool inc(); // Returns true during LY wrap around

private:
  constexpr byte_t max_ly() { return 153; }
  byte_t state{};
};

/*
 * FF45 — LYC: LY Compare Register
 *
 * The PPU continuously compares LYC with the current scanline (LY).
 * When LYC == LY:
 *   - The LYC == LY flag (STAT bit 3) is set
 *   - A STAT interrupt is requested if the LYC interrupt source is enabled
 *
 * This is a typical read/write register, so it is implemented with the generic
 * MMIORegister class since it doesn't need any additional functionality.
 */

} // namespace PPU

/*
 * 0xFF50 - Boot ROM mapping control register
 */
class BootROMCtrl : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;

  /* Determine if the boot ROM is currently mapped */
  bool boot_rom_enabled() const;
  BootROMCtrl();

private:
  bool map_boot_rom;
};

namespace Timer {

class TimerUnit {
public:
  explicit TimerUnit(bool cgb_model = true);

  void connect_if(MMIORegister& if_reg) noexcept;
  void set_cgb_model(bool cgb_model) noexcept;

  void reset() noexcept;

  // Tick by PPU-dot-based t-cycles. If double_speed=true, timer/DIV tick 2x per dot
  void tick_tcycles(std::uint32_t tcycles, bool double_speed=false) noexcept;

  // MMIO-facing helpers
  [[nodiscard]] byte_t read_div() const noexcept;
  void write_div() noexcept;

  [[nodiscard]] byte_t read_tima() const noexcept;
  void write_tima(byte_t v) noexcept;

  [[nodiscard]] byte_t read_tma() const noexcept;
  void write_tma(byte_t v) noexcept;

  [[nodiscard]] byte_t read_tac() const noexcept;
  void write_tac(byte_t v) noexcept;

private:
  [[nodiscard]] static byte_t tac_sel(byte_t tac) noexcept;
  [[nodiscard]] static bool  tac_en(byte_t tac) noexcept;
  [[nodiscard]] static bool  selected_bit(std::uint16_t sys, byte_t sel) noexcept;

  [[nodiscard]] bool edge_input(std::uint16_t sys, byte_t tac) const noexcept;
  [[nodiscard]] bool tick_allowed_on_fall() const noexcept;

  void request_timer_irq() const noexcept;

  void start_overflow_pipeline() noexcept;
  void service_overflow_pipeline() noexcept;

  void timer_tick_pulse() noexcept;
  void advance_one_tcycle() noexcept;

  std::uint16_t sys_{};
  byte_t tima_{};
  byte_t tma_{};
  byte_t tac_{};

  MMIORegister* if_reg_{};

  bool cgb_model_{true};

  // Overflow "cycle A/B"
  bool overflow_pending_{};
  std::uint8_t overflow_delay_{};

  bool reload_latch_{};
  std::uint8_t reload_delay_{};
};

// ---------------------------------------------------------------------------
// Timer registers
// ---------------------------------------------------------------------------

class DIV final : public MMIORegister {
public:
  explicit DIV(TimerUnit& t);
  void write(byte_t v) override;
  byte_t read() override;
private:
  TimerUnit& t_;
};

class TIMA final : public MMIORegister {
public:
  explicit TIMA(TimerUnit& t);
  void write(byte_t v) override;
  byte_t read() override;
private:
  TimerUnit& t_;
};

class TMA final : public MMIORegister {
public:
  explicit TMA(TimerUnit& t);
  void write(byte_t v) override;
  byte_t read() override;
private:
  TimerUnit& t_;
};

class TAC final : public MMIORegister {
public:
  explicit TAC(TimerUnit& t);
  void write(byte_t v) override;
  byte_t read() override;
private:
  TimerUnit& t_;
};

}

#endif // __MMIO_DMG_H
