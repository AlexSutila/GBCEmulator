#ifndef __MMIO_DMG_H
#define __MMIO_DMG_H

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"

class PixelProcessingUnit;
namespace PPU {

/**
 * FF40 — LCDC (LCD Control)
 *
 * Main LCD/PPU control register. Each bit enables or configures a display
 * feature.
 *
 * Bit 7 — LCD & PPU Enable
 *   0: LCD/PPU off
 *   1: LCD/PPU on
 *
 * Bit 6 — Window Tile Map Area
 *   0: 0x9800–0x9BFF
 *   1: 0x9C00–0x9FFF
 *
 * Bit 5 — Window Enable
 *   0: Window off
 *   1: Window on
 *
 * Bit 4 — BG & Window Tile Data Area
 *   0: 0x8800–0x97FF
 *   1: 0x8000–0x8FFF
 *
 * Bit 3 — BG Tile Map Area
 *   0: 0x9800–0x9BFF
 *   1: 0x9C00–0x9FFF
 *
 * Bit 2 — OBJ (Sprite) Size
 *   0: 8×8
 *   1: 8×16
 *
 * Bit 1 — OBJ (Sprite) Enable
 *   0: Sprites off
 *   1: Sprites on
 *
 * Bit 0 — BG & Window Enable / Priority
 *   DMG: 0 = BG & Window off, 1 = on
 *   CGB: Controls BG/OBJ priority behavior
 */

enum class TileMapArea : addr_t {
  LO_TILEMAP_BASE = 0x9800,
  HI_TILEMAP_BASE = 0x9C00,
};
enum class TileDataArea : addr_t {
  LO_TILEDATA_BASE = 0x8800,
  HI_TILEDATA_BASE = 0x8000,
};
enum class SpriteHeight : byte_t {
  TALL_SPRITES = 16,
  SHORT_SPRITES = 8,
};

class LCDCtrl : public MMIORegister {
public:
  void write(byte_t value) override;
  byte_t read() override;
  LCDCtrl() : state(0) {}

  /* Helpers */
  const bool lcd_enabled() const;
  const TileMapArea win_tilemap_base() const;
  const TileMapArea bg_tilemap_base() const;
  const TileDataArea bg_win_data_area() const;
  const SpriteHeight obj_size() const;
  const bool obj_enable() const;
  const bool win_enabled() const;

private:
  byte_t state{};
};

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
  const bool get_ly_eq_lyc() const;
  void set_ly_eq_lyc(bool value);
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

enum class MonoPaletteColor {
  MONO_PAL_WHITE = 0b00,
  MONO_PAL_LIIGHT_GRAY = 0b01,
  MONO_PAL_DARK_GRAY = 0b10,
  MONO_PAL_BLACK = 0b11,
};

/*
 * FF47 — BGP (BG Palette Data) [DMG / Non-CGB mode only]
 *
 * This register maps the 2-bit color indices produced by BG and Window tiles
 * to one of four grayscale shades.
 *
 * Bit layout:
 *   7–6 : Shade for color index 3
 *   5–4 : Shade for color index 2
 *   3–2 : Shade for color index 1
 *   1–0 : Shade for color index 0
 *
 * Each 2-bit shade value maps as follows:
 *   0b00 → White
 *   0b01 → Light gray
 *   0b10 → Dark gray
 *   0b11 → Black
 *
 * Note:
 *   In CGB mode, this register is ignored. BG and Window colors are instead
 *   selected from CGB palette memory (BCPS / BCPD).
 */
class BGP : public MMIORegister {
public:
  void write(byte_t value) override;
  byte_t read() override;
  BGP() : state(0) {}

  /* Indexes the internal register state to obtain true color index */
  byte_t get_color_idx(byte_t idx) const;

private:
  byte_t state{};
};

} // namespace PPU

class ObjAttrDMA;
namespace DMA {

/**
 * FF46 — DMA: OAM DMA source address & start
 *
 * Writing to this register starts a DMA transfer from ROM or RAM to OAM
 * (Object Attribute Memory).
 *
 * The written value specifies the source address divided by 0x100:
 *
 *   Source:      0xXX00–0xXX9F   (XX = 0x00 to 0xDF)
 *   Destination: 0xFE00–0xFE9F
 *
 * The transfer copies 160 bytes and takes 160 M-cycles:
 *   - 640 dots (≈1.4 scanlines) in normal speed
 *   - 320 dots (≈0.7 scanlines) in CGB double-speed mode
 *
 * This operation is significantly faster than a CPU-driven memory copy. This
 * MMIORegister derived class only serves as the interface to tell DMA to start,
 * but the underlying ObjAttrDMA class is what actually transfers data.
 */
class DMA : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  DMA(ObjAttrDMA &dma) : src_addr_base(0), dma_(dma) {}

private:
  addr_t src_addr_base{};
  ObjAttrDMA &dma_;
};

}; // namespace DMA

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

class TimerUnit;
namespace Timer {

class DIV final : public MMIORegister {
public:
  explicit DIV(TimerUnit &t);
  void write(byte_t v) override;
  byte_t read() override;

private:
  TimerUnit &t_;
};

class TIMA final : public MMIORegister {
public:
  explicit TIMA(TimerUnit &t);
  void write(byte_t v) override;
  byte_t read() override;

private:
  TimerUnit &t_;
};

class TMA final : public MMIORegister {
public:
  explicit TMA(TimerUnit &t);
  void write(byte_t v) override;
  byte_t read() override;

private:
  TimerUnit &t_;
};

class TAC final : public MMIORegister {
public:
  explicit TAC(TimerUnit &t);
  void write(byte_t v) override;
  byte_t read() override;

private:
  TimerUnit &t_;
};

} // namespace Timer

#endif // __MMIO_DMG_H
