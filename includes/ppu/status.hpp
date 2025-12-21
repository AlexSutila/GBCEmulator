#ifndef __PPU_STATUS_H
#define __PPU_STATUS_H

#include "emu_types.hpp"
#include "memory/mmio.hpp"

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
  const byte_t get_mode() const { return state & 0x03; }

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

private:
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

#endif // __PPU_STATUS_H
