#ifndef __PPU_H
#define __PPU_H

#include "cpu/interrupts.hpp"
#include "frontend/renderer.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/fifo.hpp"

#include <cstddef>
#include <optional>

class PixelProcessingUnit {
public:
  PixelProcessingUnit(AddressBus *bus_ptr);
  void connect_renderer(std::unique_ptr<Renderer> &r) { renderer = r.get(); }
  void set_cgb(const byte_t cgb_flag);
  void reset();
  void step();

private:
  AddressBus *const bus{};
  InterruptBits *ie_reg{};
  InterruptBits *if_reg{};
  Renderer *renderer{};

  /* Convenience references to important PPU mmio registers */
  PPU::LCDCtrl *lcdc_reg{};
  PPU::STAT *stat_reg{};
  PPU::LY *ly_reg{};
  MMIORegister *lyc_reg{};
  MMIORegister *scy_reg{};
  MMIORegister *scx_reg{};

  /* Pixel Processor operation modes */
  void do_oam_scan();
  void do_draw();

  /* Blanking periods */
  void do_hblank();
  void do_vblank();
  void blank();

  /* Interrupt helpers */
  void request_vblank() {
    if_reg->put_flag(InterruptFlagMask::INT_FLAG_VBLANK, true);
  }
  void request_lcd() {
    if_reg->put_flag(InterruptFlagMask::INT_FLAG_LCD, true);
  }

  /* Timing and FSM metadata */
  std::optional<std::size_t> total_mode_clks{};
  std::size_t cur_scanline_clks{};
  std::size_t cur_mode_clks{};
  std::size_t row_pixels_rendered{};

  /* Pixel FIFO renderers */
  PixelFifo bg_fifo;
  friend PixelFifo;

  /* Determined by cartridge header, dictates usable PPU features */
  bool is_cgb{};
};

#endif // __PPU_H
