#include "ppu/ppu.hpp"
#include "cart/cart.hpp"
#include "cpu/interrupts.hpp"
#include "frontend/renderer.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/fetcher.hpp"
#include "ppu/fifo.hpp"
#include "ppu/palette.hpp"

#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>

template <typename T> T *init_mmio(AddressBus *bus, IORegisterMapping reg_id) {
  auto *reg = bus->get_mmio(reg_id);
  if (auto *casted = dynamic_cast<T *>(reg))
    return casted;
  throw std::logic_error(std::string("Failed to configure MMIO (PPU)"));
}

PixelProcessingUnit::PixelProcessingUnit(AddressBus *bus, Renderer *render,
                                         runtime_sys_info &sys)
    : renderer(render), // For placing pixel data to frame buffer
      sys_(sys),        // General operating mode info
      lcdc_(),          // LCD control
      stat_(),          // PPU status
      lyc_(),           // Current scanline compare
      scy_(),           // BG scroll Y
      scx_(),           // BG scroll X
      wy_(),            // Window scroll Y
      wx_(),            // Window scroll X
      ly_(),            // Current scanline
      bgp_(),           // DMG background and window palette
      fifo(),           // Pushes background/window pixels
      cram(std::make_unique<ColorRam>()) {
  using mmio = IORegisterMapping;
  using namespace PPU;

  /* TODO: Still not the biggest fan of how we're doing this, refactor? */
  auto bgpd = cram->get_data_reg();
  auto bgpi = cram->get_idx_reg();

  /* Configure convenience MMIO register references */
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_CONTROL), &lcdc_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_STATUS), &stat_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_Y_COMP), &lyc_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_SCY), &scy_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_SCX), &scx_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_WY), &wy_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_WX), &wx_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_Y_COOR), &ly_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_BGP), &bgp_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_BGPI), bgpi);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_BGPD), bgpd);

  /* Not owned by the pixel processing unit, so have to fetch references */
  if_reg = init_mmio<InterruptBits>(bus, mmio::MMIO_INT_FLAGS);

  /* Initialize the background pixel FIFO fetching pipeline */
  bg_fetcher = std::make_unique<Fetcher>(
      bus->get_vram(), // VRAM reference for fetching tile data
      lcdc_,           // Needs to know if certain control bits are set
      scy_,            // Needed to fetch correct background tile
      scx_,            // Needed to fetch correct background tile
      wy_,             // Needed to fetch correct window tile
      wx_,             // Needed to fetch correct window tile
      ly_,             // Needed to fetch correct background tile
      fifo             // Fetcher must push rows of pixels into this FIFO
  );

  /* Configure PPU to initial state, doesn't technically happen until PPU is
   * enabled but we do it anyway just because. */
  reset();
}

void PixelProcessingUnit::set_cgb(const byte_t cgb_flag) {
  is_cgb = cgb_enabled(cgb_flag);
  bg_fetcher->set_cgb(cgb_flag);
}

bool PixelProcessingUnit::should_advance_ly() {
  constexpr std::size_t total_scanline_cycles = 456; // Fixed
  const byte_t cur_ly = ly_.read();

  /* First, perform a check to make sure we do not accidentally re-increment the
   * LY before moving onto the next frame from scanline 153, `scanline_153_bug`
   * is set to false when entering OAM scan. */
  if (cur_ly == 0 && scanline_153_bug)
    return false;

  /* Next, if we are on any scanline (including zero) without the bug enabled,
   * we simply advance to the LY register at the end of the scanline. */
  else if (cur_ly != 153 && !scanline_153_bug)
    return cur_scanline_clks >= total_scanline_cycles;

  /* Lastly, if we are on scanline 153, the value of LY is wrapped back around
   * to zero a few clock cycles in. We must set `scanline_153_bug` to true here
   * to prevent LY from being increased to one before the next frame. */
  else if (cur_ly == 153 && cur_scanline_clks >= 8) {
    scanline_153_bug = true;
    return true;
  }

  /* Catch all */
  return false;
}

/* Indexes the corresponding color palette based on the index calculated by the
 * pixel FIFO rendering pipeline. This produces an RGB value used directly by
 * our software renderer. Behavior varies between CGB and DMG modes. */
std::uint32_t PixelProcessingUnit::get_rgb(const pixel &px) const {
  if (!is_cgb) {
    const byte_t true_idx = bgp_.get_color_idx(px.color_idx);
    return get_mono_color(true_idx);
  }
  return cram->get_cgb_color(px.color_idx, px.palette_idx);
}

void PixelProcessingUnit::do_oam_scan() {
  constexpr std::size_t oam_t_cycles = 80; // Fixed
  using modes = PPU::StatModes;

  // OAM scan always happens on visible scanlines
  assert(state == modes::MODE_OAM_SCAN);
  assert(ly_.is_visible());

  // State entry
  if (!total_mode_clks.has_value()) {
    cur_scanline_clks = cur_mode_clks = 0;
    total_mode_clks = oam_t_cycles;
    scanline_153_bug = false;
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
  state = modes::MODE_DRAWING;
  total_mode_clks.reset();
}

void PixelProcessingUnit::do_draw() {
  constexpr std::size_t min_drawing_cycles = 172; // Variable
  constexpr std::size_t pixels_per_row = 160;     // H-Resolution
  using modes = PPU::StatModes;

  // Rendering always happens on visible scanlines
  assert(state == modes::MODE_DRAWING);
  assert(ly_.is_visible());

  /* By default, the PPU outputs one pixel to the screen per dot, however some
   * features cause the rendering process to stall. This additional stalling
   * time lengthens the duration of this operation mode. */
  if (!total_mode_clks.has_value()) {
    bg_fetcher->reset();
    fifo.flush();
    // Simply set to minimum, raise as quirks come up during rendering. We do
    // not use this to determine end of state.
    total_mode_clks = min_drawing_cycles;
    // We are still mid-scanline, so do not touch `cur_scanline_clks`
    cur_mode_clks = 0;
    // Counts how many pixels have been rendered on this row. This determines
    // when rendering is complete.
    row_pixels_rendered = 0;
  }

  // Step dot clock
  ++cur_scanline_clks;
  ++cur_mode_clks;
  bg_fetcher->step();

  // Rendering step, try to pop pixels when ready from the fifo
  if (fifo.can_pop()) {
    const pixel px = fifo.pop();
    if (!px.discard) {
      const auto x = row_pixels_rendered++;
      const auto y = ly_.read();
      const auto c = get_rgb(px);
      renderer->putPixel(x, y, c);
    }

    // Do we switch the fetcher into window rendering mode?
    if (bg_fetcher->is_window_visible(row_pixels_rendered))
      bg_fetcher->render_window();
  }

  // Rendering incomplete
  if (row_pixels_rendered < pixels_per_row)
    return;

  // The window uses an internal scanline counter to track it's verticle
  // rendering progress. Determine if that counter is increased (or reset)
  // here, depending on where we are in the frame.
  if (ly_.read() >= 143)
    bg_fetcher->reset_win_ly();
  else if (bg_fetcher->was_window_visible())
    bg_fetcher->inc_win_ly();

  // State transition logic
  state = modes::MODE_HBLANK;
  total_mode_clks.reset();
}

void PixelProcessingUnit::do_hblank() {
  using modes = PPU::StatModes;

  // HBlank will only ever occur during visible scanlines
  assert(state == modes::MODE_HBLANK);
  assert(ly_.is_visible());
  blank();
}

void PixelProcessingUnit::do_vblank() {
  using modes = PPU::StatModes;

  // VBlank will only ever occur during invisible scanlines - duh
  assert(state == modes::MODE_VBLANK);
  assert(!ly_.is_visible() || ly_.read() == 0);
  blank();
}

void PixelProcessingUnit::blank() {
  constexpr std::size_t total_scanline_cycles = 456;
  using modes = PPU::StatModes;

  // Use end of scanline to dictate completion of blanking
  if (!total_mode_clks.has_value())
    total_mode_clks = total_scanline_cycles;
  ++cur_scanline_clks;

  // LY will only ever be advanced during blanking. Beware it is possible for LY
  // to change mid-scanline due to the scanline 153 bug.
  if (should_advance_ly())
    ly_.inc();

  // Blanking incomplete
  if (cur_scanline_clks < total_mode_clks.value())
    return;

  // Request VBlank interrupt
  if (ly_.read() == 144)
    request_vblank_irq();

  // End of scanline logic
  if (ly_.is_visible())
    state = modes::MODE_OAM_SCAN;
  else
    state = modes::MODE_VBLANK;

  total_mode_clks.reset();
  cur_scanline_clks = 0;
}

void PixelProcessingUnit::update_stat() {
  /* The actual firing of the interrupt is fired on a rising edge of an internal
   * signal. That signal is set based on various conditions. */
  const bool old = stat_irq_signal_edge;
  stat_.set_mode(state);

  /* Condition 1: The LY register is equal to the LYC register */
  const bool cond_a = (ly_.read() == lyc_.read()) &&
                      stat_.int_enabled(PPU::StatIntFlags::LYC_EQ_LY);

  /* Condition 2: We are in HBLANK and the STAT source bit is set */
  const bool cond_b = (stat_.get_mode() == PPU::StatModes::MODE_HBLANK) &&
                      stat_.int_enabled(PPU::StatIntFlags::MODE_0_SEL);

  /* Condition 3: We are in OAM and the STAT source bit is set */
  const bool cond_c = (stat_.get_mode() == PPU::StatModes::MODE_OAM_SCAN) &&
                      stat_.int_enabled(PPU::StatIntFlags::MODE_2_SEL);

  /* Condition 4: We are in VBLANK and the STAT source bit is set. For some
   * reason, this condition is also met in OAM scan as per TCAGBD. */
  const bool cond_d = (stat_.get_mode() == PPU::StatModes::MODE_VBLANK) &&
                      (stat_.int_enabled(PPU::StatIntFlags::MODE_0_SEL) ||
                       stat_.int_enabled(PPU::StatIntFlags::MODE_1_SEL));

  // Detect rising edge, fire IRQ appropriately
  stat_irq_signal_edge = cond_a || cond_b || cond_c || cond_d;
  if (!old && stat_irq_signal_edge)
    request_lcd_irq();
  stat_.set_ly_eq_lyc(ly_.read() == lyc_.read());
}

void PixelProcessingUnit::reset() {
  using namespace PPU;
  bg_fetcher->reset();
  fifo.flush();

  /* Configure FSM timing metadata */
  cur_scanline_clks = cur_mode_clks = 0;
  total_mode_clks = std::nullopt;

  /* Configure status MMIO register initial state. The state bits read zero
   * (HBLANK) when the PPU is disabled via bit zero of the LCDC register. */
  state = StatModes::MODE_OAM_SCAN; // PPU itself always starts in OAM SCAN
  stat_.set_mode(StatModes::MODE_HBLANK);
  ly_.reset();

  /* Reset edge that triggers stat IRQs */
  stat_irq_signal_edge = false;
}

void PixelProcessingUnit::step() {

  /* When the PPU is disabled, the screen just shows plain white and the state
   * is set to it's initial state until it is re-enabled again. */
  if (!lcdc_.lcd_enabled()) [[unlikely]] {
    reset();
    return;
  }

  /* Rendering is enabled, perform FSM logic */
  switch (state) {
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

  /* Update status register */
  update_stat();
}
