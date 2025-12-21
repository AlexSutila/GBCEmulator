#include "ppu/ppu.hpp"
#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"
#include "memory/mmio.hpp"
#include "ppu/status.hpp"

#include <cassert>
#include <optional>
#include <stdexcept>

template <typename T> T *init_mmio(AddressBus *bus, IORegisterMapping reg_id) {
  auto *reg = bus->get_mmio(reg_id);
  if (auto *casted = dynamic_cast<T *>(reg))
    return casted;
  throw std::logic_error(std::string("Failed to configure MMIO (PPU)"));
}

PixelProcessor::PixelProcessor(AddressBus *bus_ptr) : bus(bus_ptr) {
  using mmio = IORegisterMapping;
  using namespace PPU;

  /* Configure FSM timing metadata */
  total_mode_clks = std::nullopt;
  cur_scanline_clks = cur_mode_clks = 0;

  /* Configure interrupts */
  ie_reg = init_mmio<InterruptBits>(bus, mmio::MMIO_INT_ENABLE);
  if_reg = init_mmio<InterruptBits>(bus, mmio::MMIO_INT_FLAGS);

  /* Initialize status MMIO registers */
  stat_reg = init_mmio<STAT>(bus, mmio::MMIO_LCD_STATUS);
  ly_reg = init_mmio<LY>(bus, mmio::MMIO_LCD_Y_COOR);
  /* LYC is just a typical R/W register, so use MMIORegister */
  lyc_reg = init_mmio<MMIORegister>(bus, mmio::MMIO_LCD_Y_COMP);

  /* Configure status MMIO registers initial state - drives FSM */
  stat_reg->set_mode(StatModes::MODE_OAM_SCAN);
  ly_reg->reset(); // Scanline zero
  lyc_reg->write(0x00);
}

void PixelProcessor::do_oam_scan() {
  constexpr std::size_t oam_t_cycles = 80; // Fixed
  using modes = PPU::StatModes;
  assert(stat_reg->get_mode() == modes::MODE_OAM_SCAN);

  // State entry
  if (!total_mode_clks.has_value()) {
    cur_scanline_clks = cur_mode_clks = 0;
    total_mode_clks = oam_t_cycles;
  }

  // TODO:
  // - Actually perform the OAM scan here

  // Step dot clock
  ++cur_scanline_clks;
  ++cur_mode_clks;

  // OAM incomplete
  if (cur_mode_clks < total_mode_clks.value())
    return;

  // State transition logic
  stat_reg->set_mode(modes::MODE_DRAWING);
  total_mode_clks.reset();
}

void PixelProcessor::do_draw() {
  constexpr std::size_t min_drawing_cycles = 172; // Variable
  using modes = PPU::StatModes;
  assert(stat_reg->get_mode() == modes::MODE_DRAWING);

  /* By default, the PPU outputs one pixel to the screen per dot, however some
   * features cause the rendering process to stall. This additional stalling
   * time lengthens the duration of this operation mode. */
  if (!total_mode_clks.has_value()) {
    // Simply set to minimum, raise as quirks come up during rendering
    total_mode_clks = min_drawing_cycles;
    // We are still mid-scanline, so do not touch `cur_scanline_clks`
    cur_mode_clks = 0;
  }

  // TODO:
  // - Actually perform rendering here

  // Step dot clock
  ++cur_scanline_clks;
  ++cur_mode_clks;

  // Rendering incomplete
  if (cur_mode_clks < total_mode_clks.value())
    return;

  // State transition logic
  stat_reg->set_mode(modes::MODE_HBLANK);
  total_mode_clks.reset();
}

void PixelProcessor::do_hblank() {
  constexpr std::size_t total_scanline_cycles = 456; // Fixed
  using modes = PPU::StatModes;

  // HBlank will only ever occur during visible scanlines
  assert(stat_reg->get_mode() == modes::MODE_HBLANK);
  assert(ly_reg->is_visible());
  blank();
}

void PixelProcessor::do_vblank() {
  constexpr std::size_t total_scanline_cycles = 456;
  using modes = PPU::StatModes;

  // VBlank will only ever occur during invisible scanlines - duh
  assert(stat_reg->get_mode() == modes::MODE_VBLANK);
  assert(!ly_reg->is_visible());
  blank();
}

void PixelProcessor::blank() {
  constexpr std::size_t total_scanline_cycles = 456;
  using modes = PPU::StatModes;

  // Use end of scanline to dictate completion of blanking
  if (!total_mode_clks.has_value())
    total_mode_clks = total_scanline_cycles;
  ++cur_scanline_clks;

  // Blanking incomplete
  if (cur_scanline_clks < total_mode_clks.value())
    return;
  ly_reg->inc();

  // End of scanline logic
  stat_reg->set_mode(ly_reg->is_visible() ? modes::MODE_OAM_SCAN
                                          : modes::MODE_VBLANK);
  total_mode_clks.reset();
  cur_scanline_clks = 0;
}

void PixelProcessor::step() {
  switch (stat_reg->get_mode()) {
  case PPU::StatModes::MODE_HBLANK:
    do_hblank();
    break;
  case PPU::StatModes::MODE_VBLANK:
    do_vblank();
    break;
  case PPU::StatModes::MODE_OAM_SCAN:
    do_oam_scan();
    break;
  case PPU::StatModes::MODE_DRAWING:
    do_draw();
    break;
  }
}
