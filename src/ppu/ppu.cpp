#include "ppu/ppu.hpp"
#include "cart/cart.hpp"
#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"

#include <cassert>
#include <optional>
#include <stdexcept>

template <typename T> T *init_mmio(AddressBus *bus, IORegisterMapping reg_id) {
  auto *reg = bus->get_mmio(reg_id);
  if (auto *casted = dynamic_cast<T *>(reg))
    return casted;
  throw std::logic_error(std::string("Failed to configure MMIO (PPU)"));
}

PixelProcessingUnit::PixelProcessingUnit(AddressBus *bus_ptr)
    : bus(bus_ptr),  // For accessing graphics memory
      lcdc_reg(),    // LCD control
      stat_reg(),    // PPU status
      ly_reg(),      // Current scanline
      lyc_reg(),     // Current scanline compare
      scy_reg(),     // BG scroll Y
      scx_reg(),     // BG scroll X
      bg_fifo(*this) // Pushes background/window pixels
{
  using mmio = IORegisterMapping;
  using namespace PPU;

  /* Configure convenience MMIO register references */
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_CONTROL), &lcdc_reg);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_STATUS), &stat_reg);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_Y_COOR), &ly_reg);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_Y_COMP), &lyc_reg);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_SCY), &scy_reg);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_SCX), &scx_reg);

  /* Not owned by the pixel processing unit, so have to fetch references */
  vbk_reg = init_mmio<VramBank>(bus, mmio::MMIO_VRAM_BANK);
  if_reg = init_mmio<InterruptBits>(bus, mmio::MMIO_INT_FLAGS);

  /* Configure PPU to initial state, doesn't technically happen until PPU is
   * enabled but we do it anyway just because. */
  reset();
}

void PixelProcessingUnit::set_cgb(const byte_t cgb_flag) {
  is_cgb = cgb_enabled(cgb_flag);
}

void PixelProcessingUnit::do_oam_scan() {
  constexpr std::size_t oam_t_cycles = 80; // Fixed
  using modes = PPU::StatModes;

  // OAM scan always happens on visible scanlines
  assert(stat_reg.get_mode() == modes::MODE_OAM_SCAN);
  assert(ly_reg.is_visible());

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
  switch_mode(modes::MODE_DRAWING);
  total_mode_clks.reset();
}

void PixelProcessingUnit::do_draw() {
  constexpr std::size_t min_drawing_cycles = 172; // Variable
  constexpr std::size_t pixels_per_row = 160;     // H-Resolution
  using modes = PPU::StatModes;

  // Rendering always happens on visible scanlines
  assert(stat_reg.get_mode() == modes::MODE_DRAWING);
  assert(ly_reg.is_visible());

  /* By default, the PPU outputs one pixel to the screen per dot, however some
   * features cause the rendering process to stall. This additional stalling
   * time lengthens the duration of this operation mode. */
  if (!total_mode_clks.has_value()) {
    // Simply set to minimum, raise as quirks come up during rendering. We do
    // not use this to determine end of state.
    total_mode_clks = min_drawing_cycles;
    // We are still mid-scanline, so do not touch `cur_scanline_clks`
    cur_mode_clks = 0;
    // Counts how many pixels have been rendered on this row. This determines
    // when rendering is complete.
    row_pixels_rendered = 0;
  }

  // Rendering step
  if (bg_fifo.can_pop()) {
    const auto pixel_data = bg_fifo.pop();
    if (renderer) // Disabled in headless mode, so this is conditional
      renderer->putPixel(row_pixels_rendered, // Denotes X-coordinate
                         ly_reg.read(),       // Denotes Y-coordinate
                         pixel_data.color);
    ++row_pixels_rendered;
  }
  bg_fifo.step();

  // Step dot clock
  ++cur_scanline_clks;
  ++cur_mode_clks;

  // Rendering incomplete
  if (row_pixels_rendered < pixels_per_row)
    return;

  // Clean fifos for next scanline
  bg_fifo.reset();

  // State transition logic
  switch_mode(modes::MODE_HBLANK);
  total_mode_clks.reset();
}

void PixelProcessingUnit::do_hblank() {
  constexpr std::size_t total_scanline_cycles = 456; // Fixed
  using modes = PPU::StatModes;

  // HBlank will only ever occur during visible scanlines
  assert(stat_reg.get_mode() == modes::MODE_HBLANK);
  assert(ly_reg.is_visible());
  blank();
}

void PixelProcessingUnit::do_vblank() {
  constexpr std::size_t total_scanline_cycles = 456;
  using modes = PPU::StatModes;

  // VBlank will only ever occur during invisible scanlines - duh
  assert(stat_reg.get_mode() == modes::MODE_VBLANK);
  assert(!ly_reg.is_visible());
  blank();

  // Render at end of frame (ly goes back to zero after blanking)
  if (ly_reg.is_visible() && renderer)
    renderer->present();
}

void PixelProcessingUnit::blank() {
  constexpr std::size_t total_scanline_cycles = 456;
  using modes = PPU::StatModes;

  // Use end of scanline to dictate completion of blanking
  if (!total_mode_clks.has_value())
    total_mode_clks = total_scanline_cycles;
  ++cur_scanline_clks;

  // Blanking incomplete
  if (cur_scanline_clks < total_mode_clks.value())
    return;
  ly_reg.inc();

  // Request VBlank interrupt
  if (ly_reg.read() == 144)
    request_vblank_irq();

  // End of scanline logic
  if (ly_reg.is_visible())
    switch_mode(modes::MODE_OAM_SCAN);
  else
    switch_mode(modes::MODE_VBLANK);

  total_mode_clks.reset();
  cur_scanline_clks = 0;
}

void PixelProcessingUnit::switch_mode(PPU::StatModes new_mode) {
  using modes = PPU::StatModes;
  stat_reg.set_mode(new_mode);

  /* Transitioning between two PPU modes may fire an LCD interrupt. */
  switch (new_mode) {
  case PPU::StatModes::MODE_HBLANK:
    if (stat_reg.int_enabled(PPU::StatIntFlags::MODE_0_SEL))
      request_lcd_irq();
    break;
  case PPU::StatModes::MODE_VBLANK:
    if (stat_reg.int_enabled(PPU::StatIntFlags::MODE_1_SEL))
      request_lcd_irq();
    break;
  case PPU::StatModes::MODE_OAM_SCAN:
    if (stat_reg.int_enabled(PPU::StatIntFlags::MODE_2_SEL))
      request_lcd_irq();
    break;
  default:
    break;
  }
}

void PixelProcessingUnit::sync_ly_lyc() {
  bool eq = lyc_reg.read() == ly_reg.read();
  bool old_bit = stat_reg.get_ly_eq_lyc();
  stat_reg.set_ly_eq_lyc(eq);

  /* We have to maintain the second bit of the STAT register and fire interrupts
   * when appropriate. This bit is always maintained unconditionally. */
  if (stat_reg.int_enabled(PPU::StatIntFlags::LYC_EQ_LY) && !old_bit && eq)
    request_lcd_irq();
}

void PixelProcessingUnit::reset() {
  using namespace PPU;
  bg_fifo.reset();

  /* Configure FSM timing metadata */
  cur_scanline_clks = cur_mode_clks = 0;
  total_mode_clks = std::nullopt;

  /* Configure status MMIO registers initial state - drives FSM */
  stat_reg.set_mode(StatModes::MODE_OAM_SCAN);
  ly_reg.reset();
}

void PixelProcessingUnit::step() {

  /* When the PPU is disabled, the screen just shows plain white and the state
   * is set to it's initial state until it is re-enabled again. */
  if (!lcdc_reg.lcd_enabled()) [[unlikely]] {
    reset();
    return;
  }

  /* Rendering is enabled, perform FSM logic */
  switch (stat_reg.get_mode()) {
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

  /* Updates LY=LYC status bit, and fires interrupt if appropriate. */
  sync_ly_lyc();
}
