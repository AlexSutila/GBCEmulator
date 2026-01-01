#ifndef __PPU_H
#define __PPU_H

#include "cpu/interrupts.hpp"
#include "frontend/renderer.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/fetcher.hpp"
#include "ppu/fifo.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

class PixelProcessingUnit {
public:
  PixelProcessingUnit(AddressBus *bus, Renderer *renderer);
  void set_cgb(const byte_t cgb_flag);
  void reset();
  void step();

private:
  Renderer *const renderer{};
  InterruptBits *if_reg{};

  /* Convenience references to important PPU mmio registers */
  PPU::VramBank *vbk_reg{};
  PPU::LCDCtrl lcdc_{};
  PPU::STAT stat_{};
  MMIORegister lyc_{};

  /* Background and window positional registers */
  MMIORegister scy_{};
  MMIORegister scx_{};
  MMIORegister wy_{};
  MMIORegister wx_{};

  /* For tracking where we currently are in the rendering process */
  std::size_t row_pixels_rendered{};
  bool should_advance_ly();
  bool scanline_153_bug{};
  PPU::LY ly_{};

  /* Color palette configuration */
  std::uint32_t get_rgb(byte_t idx) const;
  PPU::BGP bgp_{};

  /* Pixel Processor operation modes */
  void do_oam_scan();
  void do_draw();

  /* Blanking periods */
  void do_hblank();
  void do_vblank();
  void blank();

  /* Interrupt helpers */
  void request_vblank_irq() {
    if_reg->put_flag(InterruptFlagMask::INT_FLAG_VBLANK, true);
  }
  void request_lcd_irq() {
    if_reg->put_flag(InterruptFlagMask::INT_FLAG_LCD, true);
  }
  bool stat_irq_signal_edge{};
  void update_stat();

  /* Timing and FSM metadata */
  std::optional<std::size_t> total_mode_clks{};
  std::size_t cur_scanline_clks{};
  std::size_t cur_mode_clks{};
  PPU::StatModes state{};

  /* Pixel FIFO renderers */
  std::unique_ptr<Fetcher> bg_fetcher;
  PixelFifo fifo;

  /* Determined by cartridge header, dictates usable PPU features */
  bool is_cgb{};
};

#endif // __PPU_H
