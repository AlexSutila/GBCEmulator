#ifndef __MMIO_CGB_H
#define __MMIO_CGB_H

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"
#include <array>

namespace PPU {

/*
 * FF4F - VBK: VRAM Bank
 *
 * This register can be written to change VRAM banks. Only bit 0 matters, all
 * other bits are ignored.
 */
class VramBank : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  VramBank() : MMIORegister(0), state(0) {}

  constexpr bool cgb() override { return true; }
  const byte_t get_bank() const;

private:
  byte_t state{};
};

/**
 * Background Color Palette Specification / Background Palette Index
 *
 * Addressing order:
 *   BGP0 color 0 (low, high),
 *   BGP0 color 1 (low, high),
 *   BGP0 color 2 (low, high),
 *   BGP0 color 3 (low, high),
 *   BGP1 color 0 (low, high), ...
 *
 * Bit layout:
 *   Bit 7   Auto-increment
 *           0 = Disabled
 *           1 = Increment Address after writing to BCPD
 *               (increment occurs even during Mode 3, although the write itself
 *                fails; reads never cause an increment)
 *   Bits 6-0 Address
 *           Index (0–63) of the byte in BG palette RAM accessed via BCPD
 */

class PaletteIdx : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  PaletteIdx() : state(0) {}
  constexpr bool cgb() override { return true; }

  // Writes to color RAM can increase register value
  bool auto_inc_enabled() const;
  void inc();
  // Index color RAM contents
  addr_t get_address() const;

private:
  byte_t state{};
};

class PaletteData : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  PaletteData(std::array<byte_t, 64> &mem, PaletteIdx &idx);
  constexpr bool cgb() override { return true; }

private:
  std::array<byte_t, 64> &mem_;
  PaletteIdx &idx_;
  byte_t state{};
};

} // namespace PPU

/*
 * FF70 - SVBK/WBK: WRAM Bank
 *
 * In CGB Mode, 32 KiB of internal RAM are available. This memory is divided
 * into 8 banks of 4 KiB each. Bank 0 is always available in memory at
 * C000–CFFF, banks 1–7 can be selected into the address space at D000–DFFF.
 */
class WramBank : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  WramBank() : MMIORegister(0), state(1) {}

  constexpr bool cgb() override { return true; }
  const byte_t get_bank() const;

private:
  byte_t state{};
};

#endif // __MMIO_CGB_H
