#ifndef __PPU_H
#define __PPU_H

#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/status.hpp"

#include <cstddef>
#include <optional>

class PixelProcessor {
public:
  PixelProcessor(AddressBus *bus_ptr);
  void set_cgb(const byte_t cgb_flag);
  void step();

private:
  AddressBus *const bus{};
  InterruptBits *ie_reg{};
  InterruptBits *if_reg{};

  /* Pixel Processor status registers */
  PPU::STAT *stat_reg{};
  PPU::LY *ly_reg{};
  MMIORegister *lyc_reg{};

  /* Pixel Processor operation modes */
  void do_oam_scan();
  void do_draw();

  /* Blanking periods */
  void do_hblank();
  void do_vblank();
  void blank();

  /* Timing and FSM metadata */
  std::optional<std::size_t> total_mode_clks{};
  std::size_t cur_scanline_clks{};
  std::size_t cur_mode_clks{};

  /* Interrupt helpers */
  void request_vblank() {
    if_reg->put_flag(InterruptFlagMask::INT_FLAG_VBLANK, true);
  }
  void request_lcd() {
    if_reg->put_flag(InterruptFlagMask::INT_FLAG_LCD, true);
  }

  /* Determined by cartridge header, dictates usable PPU features */
  bool is_cgb{};
};

#endif // __PPU_H
