#ifndef __MMIO_CGB_H
#define __MMIO_CGB_H

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"
#include <array>
#include <cstddef>

struct runtime_sys_info;

namespace SYS {

/*
 *  Bit 7 6 5 4 3           2            1 0
 * KEY0           DMG compatibility mode
 *      - - - - - ---------------------- - -
 * DMG compatibility mode:
 *   0 = Disabled (full CGB mode, for regular CGB cartridges)
 *   1 = Enabled  (for DMG-only cartridges)
 */
class KEY0 final : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  KEY0(runtime_sys_info &sys) : sys_(sys), state(0) {}

private:
  static constexpr byte_t dmg_mode_mask = 0x04;
  runtime_sys_info &sys_;
  byte_t state{};
};

/*
 * FF4D — KEY1/SPD (CGB mode only): Prepare speed switch
 *
 * Bit layout:
 *   7   6   5   4   3   2   1   0
 *   -   -   -   -   -   -   -   A
 *   Bit 7 — Current speed (read-only):
 *            0 = Normal-speed mode
 *            1 = Double-speed mode
 *   Bit 0 — Switch armed (read/write):
 *            0 = Not armed
 *            1 = Armed (prepare speed switch)
 *   Bits 6–1: Unused
 */
class KEY1 final : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  KEY1(runtime_sys_info &sys) : sys_(sys), state(0) {}

private:
  static constexpr byte_t unused_bits_mask = 0x7E;
  runtime_sys_info &sys_;
  byte_t state{};
};

} // namespace SYS

namespace PPU {

/*
 * FF4F - VBK: VRAM Bank
 *
 * This register can be written to change VRAM banks. Only bit 0 matters, all
 * other bits are ignored.
 */
class VramBank final : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  VramBank() : MMIORegister(0), state(0) {}
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

class PaletteIdx final : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  PaletteIdx() : state(0) {}

  // Writes to color RAM can increase register value
  bool auto_inc_enabled() const;
  void inc();
  // Index color RAM contents
  addr_t get_address() const;

private:
  byte_t state{};
};

class PaletteData final : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  PaletteData(std::array<byte_t, 64> &mem, PaletteIdx &idx);

private:
  std::array<byte_t, 64> &mem_;
  PaletteIdx &idx_;
  byte_t state{};
};

/*
 * FF6C — OPRI (CGB Mode only): Object Priority Mode
 *
 * Bit layout:
 *   7   6   5   4   3   2   1   0
 *   OPRI                  Priority mode
 *
 * Priority mode (Read/Write):
 *   0 = CGB-style priority
 *   1 = DMG-style priority
 */
enum class ObjectPriorityMode {
  OPRI_CGB_STYLE = 0,
  OPRI_DMG_STYLE = 1,
};

class OPRI final : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  OPRI() : state(0) {}

  /* Resolves sprite ordering in OAM search */
  const ObjectPriorityMode get_prio_mode() const;
  static constexpr byte_t unused_mask = 0xFE;

private:
  byte_t state{};
};

} // namespace PPU

class VDMA;
namespace DMA {

/*
 * FF51–FF55 — CGB VRAM DMA (HDMA)
 *
 * FF51–FF52 (HDMA1/2): Source address (write-only)
 *   - Transfer source in ROM, SRAM, or WRAM:
 *       0000–7FF0 or A000–DFF0
 *   - Lower 4 bits are ignored (aligned to 0x10)
 *   - Using VRAM as a source results in garbage data
 *
 * FF53–FF54 (HDMA3/4): Destination address (write-only)
 *   - Destination in VRAM: 8000–9FF0
 *   - Only bits 12–4 are used; lower 4 bits ignored (aligned to 0x10)
 *   - Upper bits are ignored (always VRAM)
 *
 * FF55 (HDMA5): Length / Mode / Start
 *   - Writing starts the DMA transfer
 *   - Bits 0–6: Transfer length = (value + 1) * 0x10 bytes (0x10–0x800)
 *   - Bit 7:
 *       0 = General DMA
 *           * Transfers all data at once
 *           * CPU halted until completion
 *           * Must be used with LCD off, VBlank, or short HBlank-safe blocks
 *           * FF55 reads as 0xFF when complete
 *       1 = HBlank DMA
 *           * Transfers 0x10 bytes per HBlank (LY 0–143)
 *           * Pauses during VBlank, resumes at LY=0
 *           * CPU halted only during each block
 *           * Source/destination banks must not change during transfer
 *           * Writing Bit 7 = 0 aborts transfer (remaining blocks preserved)
 *
 *   - Reading FF55:
 *       * Lower 7 bits: remaining blocks minus 1
 *       * 0xFF indicates transfer complete
 */

enum class VDMATransferMode {
  GENERAL_PURPOSE_DMA = 0,
  HBLANK_DMA = 1,
};

// AKA: VDMA5
class VDMA_MODE_LEN final : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  VDMA_MODE_LEN(VDMA &dma) : state(0), dma_(dma) {}
  void signal_complete() { state = 0xFF; }

  const VDMATransferMode get_mode() const;
  const std::size_t get_size_bytes() const;
  const std::size_t get_size_blks() const;

private:
  byte_t state{};
  VDMA &dma_;
};

} // namespace DMA
/*
 * FF70 - SVBK/WBK: WRAM Bank
 *
 * In CGB Mode, 32 KiB of internal RAM are available. This memory is divided
 * into 8 banks of 4 KiB each. Bank 0 is always available in memory at
 * C000–CFFF, banks 1–7 can be selected into the address space at D000–DFFF.
 */
class WramBank final : public MMIORegister {
public:
  void write(const byte_t value) override;
  byte_t read() override;
  WramBank() : MMIORegister(0), state(1) {}
  const byte_t get_bank() const;

private:
  byte_t state{};
};

#endif // __MMIO_CGB_H
