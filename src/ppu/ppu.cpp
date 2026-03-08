#include "ppu/ppu.hpp"
#include "cpu/interrupts.hpp"
#include "debugger/breakpoint.hpp"
#include "frontend/frontend.hpp"
#include "gbc.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/fetcher.hpp"
#include "ppu/fifo.hpp"
#include "ppu/palette.hpp"
#include "ppu/pixel.hpp"
#include "ppu/sprites.hpp"
#include "savestate/codec.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

// Store DMG color index in alpha bits bc we're just based like that lmao
#define DMG_COLOR_PRESERVE_HACK(rgb, idx) ((rgb & 0x00FFFFFF) | (idx << 24))

enum : std::uint16_t {
  F_FLUSH_ON_DISABLE = 1,
  F_ROW_PIXELS_RENDERED,
  F_SPRITES_FETCHED,
  F_SPRITES_SEARCHED,
  F_SCANLINE_153_BUG,
  F_PPU_ENABLE_OAM_BUG,
  F_STAT_IRQ_EDGE,
  F_TOTAL_MODE_CLKS,
  F_CUR_SCANLINE_CLKS,
  F_CUR_MODE_CLKS,
  F_STATE,

  // Complex types, leverage recursive descent
  F_FETCHER,
  F_OBJ_FIFO,
  F_BG_FIFO,
  F_OBJ_CRAM,
  F_BG_CRAM,
  F_STAT_DELAY,
  F_STAT_DELAY_STATE, // Inner state values for `F_STAT_DELAY`
  F_OAM_DATA,

  // MMIO registers
  F_LCDC,
  F_STAT,
  F_LYC,
  F_SCY,
  F_SCX,
  F_WY,
  F_WX,
  F_LY,
  F_BGP,
  F_OBP0,
  F_OBP1,
  F_OPRI,
};

enum : std::uint16_t {
  F_SPRITE_Y = 1,
  F_SPRITE_X,
  F_SPRITE_TILE_IDX,
  F_SPRITE_TILE_ATTR,
  F_SPRITE_OBJ_NO,
};

template <typename T> void PixelProcessingUnit::parse_savestate(T &t) {
  constexpr auto version = 1; // Schema revision
  t.chunk_header(version, Savestate::C_PPU);

  t.field_generic(F_FLUSH_ON_DISABLE, flush_on_disable);
  t.field_generic(F_ROW_PIXELS_RENDERED, row_pixels_rendered);
  t.field_generic(F_SPRITES_FETCHED, sprites_fetched);
  t.field_generic(F_SPRITES_SEARCHED, sprites_searched);
  t.field_generic(F_SCANLINE_153_BUG, scanline_153_bug);
  t.field_generic(F_PPU_ENABLE_OAM_BUG, ppu_enable_oam_bug);
  t.field_generic(F_STAT_IRQ_EDGE, stat_irq_signal_edge);
  t.field_generic(F_CUR_SCANLINE_CLKS, cur_scanline_clks);
  t.field_generic(F_CUR_MODE_CLKS, cur_mode_clks);
  t.field_optional(F_TOTAL_MODE_CLKS, total_mode_clks);
  t.field_enum(F_STATE, state);

  // Complex sub-structures
  t.field_complex(F_FETCHER, [&](T &t) { fetcher->parse_savestate(t); });
  t.field_complex(F_OBJ_FIFO, [&](T &t) { obj_fifo.parse_savestate(t); });
  t.field_complex(F_BG_FIFO, [&](T &t) { bg_fifo.parse_savestate(t); });
  t.field_complex(F_OBJ_CRAM, [&](T &t) { obj_cram->parse_savestate(t); });
  t.field_complex(F_BG_CRAM, [&](T &t) { bg_cram->parse_savestate(t); });
  t.field_complex(F_STAT_DELAY, [&](T &t) {
    stat_delay.parse_savestate(t, [](auto &t, PPU::StatModes &s) {
      t.field_enum(F_STAT_DELAY_STATE, s);
    });
  });
  t.field_vector(F_OAM_DATA, oam_data, max_oam_sprite_count,
                 [&](T &t, auto &s) {
                   t.field_generic(F_SPRITE_Y, s.y_pos);
                   t.field_generic(F_SPRITE_X, s.x_pos);
                   t.field_generic(F_SPRITE_TILE_IDX, s.tile_idx);
                   t.field_generic(F_SPRITE_TILE_ATTR, s.tile_attr);
                   t.field_generic(F_SPRITE_OBJ_NO, s.obj_no);
                 });

  // Memory mapped IO registers
  t.field_complex(F_LCDC, [&](T &t) { lcdc_.parse_savestate(t); });
  t.field_complex(F_STAT, [&](T &t) { stat_.parse_savestate(t); });
  t.field_complex(F_LYC, [&](T &t) { lyc_.parse_savestate(t); });
  t.field_complex(F_SCY, [&](T &t) { scy_.parse_savestate(t); });
  t.field_complex(F_SCX, [&](T &t) { scx_.parse_savestate(t); });
  t.field_complex(F_WY, [&](T &t) { wy_.parse_savestate(t); });
  t.field_complex(F_WX, [&](T &t) { wx_.parse_savestate(t); });
  t.field_complex(F_BGP, [&](T &t) { bgp_.parse_savestate(t); });
  t.field_complex(F_OBP0, [&](T &t) { obp0_.parse_savestate(t); });
  t.field_complex(F_OBP1, [&](T &t) { obp1_.parse_savestate(t); });
  t.field_complex(F_OPRI, [&](T &t) { opri_.parse_savestate(t); });

  t.eof();
}

template void
PixelProcessingUnit::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void
PixelProcessingUnit::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void
PixelProcessingUnit::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);

template <typename T>
T *init_mmio(AddressBus *bus, const IORegisterMapping reg_id) {
  auto *reg = bus->get_mmio(reg_id);
  if (auto *casted = dynamic_cast<T *>(reg))
    return casted;
  throw std::logic_error(std::string("Failed to configure MMIO (PPU)"));
}

PixelProcessingUnit::PixelProcessingUnit(
    AddressBus *bus, Frontend &fe, std::optional<Debug::Debugger> &debugger,
    runtime_sys_info &sys)
    : Debuggable(debugger),   // Scanline/frame breakpoints
      sys_(sys),              // General operating mode info
      fe_(fe),                // To access frame buffer(s)
      vram(bus->get_vram()),  // Tile data/map/attribute content
      oam(bus->get_oam()),    // Object (sprite) attribute memory
      vdma_(bus->get_vdma()), // Performs GDMA and HDMA in CGB mode
      obj_cram(std::make_unique<ColorRam>()), // CGB sprite color RAM
      bg_cram(std::make_unique<ColorRam>())   // CGB background color RAM
{
  using mmio = IORegisterMapping;
  using namespace PPU;

  /* These registers are managed by our implementation of the RGB555 color
   * palette system, so grab the reference rq so we can hold onto them. */
  auto bgpd = bg_cram->get_data_reg(), obpd = obj_cram->get_data_reg();
  auto bgpi = bg_cram->get_idx_reg(), obpi = obj_cram->get_idx_reg();

  /* Configure MMIO register connections over address bus */
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_CTRL), &lcdc_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_STAT), &stat_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_Y_COMP), &lyc_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_SCY), &scy_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_SCX), &scx_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_WY), &wy_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_WX), &wx_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_Y_COOR), &ly_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_BGP), &bgp_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_OBP0), &obp0_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_OBP1), &obp1_);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_BGPI), bgpi);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_BGPD), bgpd);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_OBPI), obpi);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_LCD_OBPD), obpd);

  /* Not owned by the pixel processing unit, so have to fetch references */
  if_reg = init_mmio<InterruptBits>(bus, mmio::MMIO_INT_FLAGS);

  /* Initialize the background and object pixel FIFO fetching pipeline */
  fetcher = std::make_unique<Fetcher>(
      bus->get_vram(), // VRAM reference for fetching tile data
      lcdc_,           // Needs to know if certain control bits are set
      scy_,            // Needed to fetch correct background tile
      scx_,            // Needed to fetch correct background tile
      wy_,             // Needed to fetch correct window tile
      wx_,             // Needed to fetch correct window tile
      opri_,           // Pixel overwrite in OBJ FIFO is determined by priority
      ly_,             // Needed to fetch correct background tile
      obj_fifo,        // Fetcher stalls BG fetch to populate this when needed
      bg_fifo,         // Fetcher must push rows of pixels into this FIFO
      sys_             // Fetcher behavior varies between DMG vs CGB mode
  );

  /* Initialize OAM search metadata */
  oam_data.reserve(max_oam_sprite_count);

  /* Configure PPU to initial state, doesn't technically happen until PPU is
   * enabled but we do it anyway just because. */
  reset();

  /* This needs to be initialized to false by default, but the actual high/low
   * value of this signal is persistent across PPU enable and disable. */
  stat_irq_signal_edge = false;
}

PixelProcessingUnit::PPUState PixelProcessingUnit::get_state() const {
  PPUState state_{};
  state_.state = state; // Does not necessarily match STAT due to delay
  state_.lcdc = lcdc_.peek();
  state_.stat = stat_.peek();
  state_.scx = scx_.peek();
  state_.scy = scy_.peek();
  state_.wy = wy_.peek();
  state_.wx = wx_.peek();
  state_.lyc = lyc_.peek();
  state_.ly = ly_.peek();
  state_.dots = cur_scanline_clks;
  return state_;
}

bool PixelProcessingUnit::should_advance_ly() {
  constexpr std::size_t total_scanline_cycles = 456; // Fixed

  /* First, perform a check to make sure we do not accidentally re-increment the
   * LY before moving onto the next frame from scanline 153, `scanline_153_bug`
   * is set to false when entering OAM scan. */
  if (const byte_t cur_ly = ly_.peek(); cur_ly == 0 && scanline_153_bug)
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
 * our software renderer. Behavior varies between CGB and DMG modes.
 * ----------------------------------------------------------------------------
 * To support both colored and monochrome modes in DMG mode, we abuse the alpha
 * bits here to save some storage space and store the index into a monochrome
 * palette in addition to the actual RGB color. */
std::uint32_t PixelProcessingUnit::get_bgwin_rgb(const pixel &px) const {
  if (!sys_.cgb_mode) {
    if (!lcdc_.bg_win_en_priority()) // DMG renders white when bg enable is off
      return DMG_COLOR_PRESERVE_HACK(0x00FFFFFFFF, 0);
    /* Otherwise if we are running in backwards compatability mode, we have to
     * consult the BGP register to translate the monochrome color index. */
    const byte_t true_color_idx = bgp_.get_color_idx(px.color_idx);
    const std::uint32_t rgb = bg_cram->get_cgb_color(true_color_idx, 0);
    return DMG_COLOR_PRESERVE_HACK(rgb, true_color_idx);
  }
  // CGB palette is denoted directly by the attributes themselves
  return bg_cram->get_cgb_color(px.color_idx, px.palette_idx);
}
std::uint32_t PixelProcessingUnit::get_obj_rgb(const pixel &px) const {
  if (!sys_.cgb_mode) {
    /* If we are running in backwards compatability mode, we have to consult one
     * of the OBP0/OBP1 registers to translate the monochrome color index. */
    const byte_t palette_idx = px.palette_idx & 0x1;
    const byte_t true_color_idx = (palette_idx == 0)
                                      ? obp0_.get_color_idx(px.color_idx)
                                      : obp1_.get_color_idx(px.color_idx);
    const std::uint32_t rgb =
        obj_cram->get_cgb_color(true_color_idx, palette_idx);
    return DMG_COLOR_PRESERVE_HACK(rgb, true_color_idx);
  }
  // CGB palette is denoted directly by the attributes themselves
  return obj_cram->get_cgb_color(px.color_idx, px.palette_idx);
}

/* Determines if a sprite is visible on the current pixel being processed. This
 * method will ultimately end up determining when sprites need to be fetched. */
bool PixelProcessingUnit::next_sprite_visible(std::size_t px_idx) const {
  constexpr auto max_sprites = 10; // Per-scanline hardware limitation
  if (sprites_fetched >= oam_data.size() || sprites_fetched >= max_sprites)
    return false;

  // We make the assumption that this sprite lies along the scanline vertically
  const Sprite &next_sprite = oam_data.at(sprites_fetched);
  return sprite_visible(next_sprite.x_pos, px_idx);
}

/* Performs the fetcher stepping, FIFO popping, and all the logic behind what
 * happens when regarding the pixel FIFO madness that confuses everyone. */
std::optional<std::uint32_t>
PixelProcessingUnit::get_next_pixel(const std::size_t px_idx) {

  // If the window becomes visible, we have to reset the fetcher so it starts
  // fetching window data instead of BG data.
  if (fetcher->is_window_visible(row_pixels_rendered))
    fetcher->render_window();

  // No sprite interaction occurs with this pixel, so ignore OAM data.
  if (!next_sprite_visible(px_idx)) {
    fetcher->step();
    return try_fifo_pop();
  }

  // The remaining code path is now sprite-aware. Pixel data for the next pixel
  // for both sprites and BG must be fetched before we can pop again.
  const Sprite &next_sprite = oam_data.at(sprites_fetched);

  // Here, the BG and sprites fight over fetch time until we have both a sprite
  // pixel and a background or window pixel to combine. Stall if not done.
  if (fetcher->step_and_try_sprite_fetch(next_sprite)) {
    ++sprites_fetched;

    // If no further sprite is to be rendered with this pixel, we can emit it.
    // There is still a change a sprite overlaps the same starting pixel.
    if (!next_sprite_visible(px_idx)) [[unlikely]]
      return try_fifo_pop();
  }

  // Catch all scenario, pixel just isn't ready yet
  return std::nullopt;
}

std::uint32_t
PixelProcessingUnit::resolve_px_priority(const pixel &bg_px,
                                         const pixel &obj_px) const {
  const bool lcdc = lcdc_.bg_win_en_priority();
  const bool oam_ = obj_px.take_priority;
  const bool bg = bg_px.take_priority;

  // If background color index is zero, sprites always have priority
  if (bg_px.color_idx == 0)
    return get_obj_rgb(obj_px);

  // This is the 'fighting over priority' that is mentioned numerous places
  // throughout this codebase. It isn't actually that bad, I was just lazy.
  if (lcdc && (oam_ || bg))
    return get_bgwin_rgb(bg_px);
  return get_obj_rgb(obj_px);
}

std::optional<std::uint32_t> PixelProcessingUnit::try_fifo_pop() {
  if (!bg_fifo.can_pop())
    return std::nullopt;

  // Pop the background pixel, try to pop the sprite FIFO. If the sprite FIFO
  // is empty, just proceed. The sprite FIFO will be populated on demand.
  const pixel bg_px = bg_fifo.pop();
  if (!obj_fifo.can_pop())
    return get_bgwin_rgb(bg_px);

  // If we can pop a pixel from the sprite FIFO, we merge it with the background
  // pixel in the background FIFO. This is why we must have a background pixel
  // to accompany any pixels in the sprite FIFO, and not the other way around.
  pixel obj_px = obj_fifo.pop();
  if (!lcdc_.obj_enable())
    obj_px.color_idx = 0;

  // May need to discard the pixel due to SCX fine scrolling
  if (is_transparent(obj_px)) // If object is transparent use BG
    return get_bgwin_rgb(bg_px);

  // Otherwise, render whatever, let the two pixels fight over priority.
  return resolve_px_priority(bg_px, obj_px);
}

/* ======================================================================
 * Core Pixel Processing Unit Behavior Implementation Below
 * ====================================================================== */

void PixelProcessingUnit::do_disabled() {
  if (flush_on_disable) {
    fe_.clear(); // This is slow
    reset();

    // Disabling the PPU impacts the other PPU related registers
    stat_.set_mode(PPU::StatModes::MODE_HBLANK);
    ly_.write(0); // Start at first scanline

    /* Reset PPU state only once when it is disabled. */
    flush_on_disable = false;
  }
}

void PixelProcessingUnit::do_oam_scan() {
  constexpr auto max_sprites = 10; // Per-scanline hardware limitation
  using modes = PPU::StatModes;

  // OAM scan always happens on visible scanlines
  assert(state == modes::MODE_OAM_SCAN);
  assert(ly_.is_visible());

  // State entry
  if (!total_mode_clks.has_value()) {
    constexpr std::size_t oam_t_cycles = 80;
    cur_scanline_clks = cur_mode_clks = 0;
    total_mode_clks = oam_t_cycles;
    scanline_153_bug = false;

    /* Handle strange timing on first scanline of PPU being enabled. The modes
     * which follow OAM are supposedly unimpacted. */
    if (ppu_enable_oam_bug) [[unlikely]]
      total_mode_clks = oam_t_cycles - 2; // Hardware bug

    /* State entry always indicates the start of a new scanline, but if the LY
     * register currently reads zero, we have also begun a new frame too. */
    const Debug::BreakReason reason =
        (ly_.peek() == 0) ? Debug::BRK_STEP_SCANLINE | Debug::BRK_STEP_FRAME
                          : Debug::BRK_STEP_SCANLINE;
    try_brk(reason);

    /* Keeps track of which sprite we are on being on. If the sprite is visible
     * on the current scanline, we push it into the vector to so all the sprites
     * which need to be rendered can be tracked. */
    sprites_searched = 0;
    oam_data.clear();
  }

  // TODO: Unsure about if this is sampled here or not. Research needed.
  const bool tall = lcdc_.obj_size() == PPU::SpriteHeight::TALL_SPRITES;

  /* Linear object attribute memory scanning begins here!!!!!
   * ---------------------------------------------------------------------------
   * Check one sprite every two clocks. Because we are indexing object attribute
   * memory array directly, we don't need to consider the base address of object
   * attribute memory. */
  if (cur_mode_clks % 2 == 0 && !ppu_enable_oam_bug) {
    const addr_t sprite_base_offset = sprite_size_bytes * sprites_searched;
    const byte_t y_pos = oam[sprite_base_offset + oam_y_offset];
    const byte_t x_pos = oam[sprite_base_offset + oam_x_offset];
    const byte_t attrs = oam[sprite_base_offset + oam_attr_offset];
    const byte_t index = oam[sprite_base_offset + oam_tile_idx_offset];

    // Worry about ordering later, enough space is reserved ahead of time such
    // that no unnecessary memory copies occur when the vector fills up. I am
    // not 100% sure, but I am pretty sure obj enable bit impacts OAM scan.
    if (lcdc_.obj_enable() &&                           // Are sprites enabled?
        oam_data.size() < max_sprites &&                // Is OAM cache full?
        sprite_visible(x_pos, y_pos, ly_.peek(), tall)) // Sprite is visible?
      oam_data.push_back({
          .y_pos = y_pos,
          .x_pos = x_pos,
          .tile_idx = index,
          .tile_attr = attrs,
          .obj_no = sprites_searched,
      });
    ++sprites_searched;
  }

  // Step dot clock
  ++cur_scanline_clks;
  ++cur_mode_clks;

  // OAM incomplete
  if (cur_mode_clks < total_mode_clks.value())
    return;

  /* At this point, this vector contains an array of sprites which are visible
   * on the current scanline. Since the renderer goes from left to right, any
   * sprites are also rendered in that order during the drawing state as pixels
   * are pushed onto the LCD. Hence, sort by `x_pos`. */
  auto selection_priority = [](const Sprite &a, const Sprite &b) {
    return (a.x_pos == b.x_pos)
               ? a.obj_no < b.obj_no // OAM index is used to break any ties
               : a.x_pos < b.x_pos;  // Otherwise sort based on X-position
  };
  std::ranges::sort(oam_data, selection_priority);

  // State transition logic
  state = modes::MODE_DRAWING;
  total_mode_clks.reset();

  if (ppu_enable_oam_bug) [[unlikely]]
    ppu_enable_oam_bug = false;
}

void PixelProcessingUnit::do_draw() {
  using modes = PPU::StatModes;

  // Rendering always happens on visible scanlines
  assert(state == modes::MODE_DRAWING);
  assert(ly_.is_visible());

  /* By default, the PPU outputs one pixel to the screen per dot, however some
   * features cause the rendering process to stall. This additional stalling
   * time lengthens the duration of this operation mode. */
  if (!total_mode_clks.has_value()) {
    constexpr std::size_t min_drawing_cycles = 172;
    fetcher->reset();
    obj_fifo.flush();
    bg_fifo.flush();
    // Simply set to minimum, raise as quirks come up during rendering. We do
    // not use this to determine end of state.
    total_mode_clks = min_drawing_cycles;
    cur_mode_clks = 0;
    // Counts how many pixels have been rendered on this row. The first var
    // here determines when rendering is complete.
    row_pixels_rendered = sprites_fetched = 0;
  }

  // Step dot clock
  ++cur_scanline_clks;
  ++cur_mode_clks;

  // Rendering step, try to pop pixels when ready from the fifo
  if (const auto px = get_next_pixel(row_pixels_rendered); px.has_value()) {
    const auto x = row_pixels_rendered++;
    const auto y = ly_.peek();
    const auto c = px.value();
    fe_.put_pixel(static_cast<int>(x), y, c);
  }

  // Rendering incomplete
  if (constexpr std::size_t pixels_per_row = 160;
      row_pixels_rendered < pixels_per_row)
    return;

  // The window uses an internal scanline counter to track it's vertical
  // rendering progress. Determine if that counter is increased (or reset)
  // here, depending on where we are in the frame.
  if (ly_.peek() >= 143)
    fetcher->reset_win_ly();
  else if (fetcher->was_window_visible())
    fetcher->inc_win_ly();

  // TODO: I am not 100% sure about the sample timing of this, but I do know
  // with a high degree of certainty that it is only sampled once per scanline
  fetcher->sample_window_enable();

  /* Signal that HDMA can start running if it has been requested or started
   * previously. If HBLANK is partially complete, it can also be triggered. */
  vdma_.set_ppu_hblank_signal(true);

  // State transition logic
  state = modes::MODE_HBLANK;
  total_mode_clks.reset();
}

void PixelProcessingUnit::do_hblank() {
  // HBlank will only ever occur during visible scanlines
  assert(state == PPU::StatModes::MODE_HBLANK);
  assert(ly_.is_visible());

  /* Signal that HDMA is no longer allowed to kick in. Note, that it can still
   * start running last minute and bleed into OAM scan. This is intentional. */
  if (blank())
    vdma_.set_ppu_hblank_signal(false);
}

void PixelProcessingUnit::do_vblank() {
  // VBlank will only ever occur during invisible scanlines - duh
  assert(state == PPU::StatModes::MODE_VBLANK);
  assert(!ly_.is_visible() || ly_.peek() == 0);
  blank();
}

bool PixelProcessingUnit::blank() {
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
    return false;

  // Request VBlank interrupt
  if (ly_.peek() == 144)
    request_vblank_irq();

  // End of scanline logic
  if (ly_.is_visible())
    state = modes::MODE_OAM_SCAN;
  else
    state = modes::MODE_VBLANK;

  total_mode_clks.reset();
  cur_scanline_clks = 0;
  return true;
}

void PixelProcessingUnit::update_stat(const PPU::StatModes new_mode) {
  const bool old = stat_irq_signal_edge;
  const auto &cur_mode = state;

  /* Handle STAT mode bits reading wrong value for first scanline upon the PPU
   * being enabled after not being enabled. */
  if (ppu_enable_oam_bug && new_mode == PPU::StatModes::MODE_OAM_SCAN)
      [[unlikely]]
    stat_.set_mode(PPU::StatModes::MODE_HBLANK); // Hardware bug
  else [[likely]]
    stat_.set_mode(new_mode);

  /* Condition 1: The LY register is equal to the LYC register */
  const bool cond_a = ly_.peek() == lyc_.peek() &&
                      stat_.int_enabled(PPU::StatIntFlags::LYC_SEL);

  /* Condition 2: We are in HBLANK and the STAT source bit is set */
  const bool cond_b = cur_mode == PPU::StatModes::MODE_HBLANK &&
                      stat_.int_enabled(PPU::StatIntFlags::MODE_0_SEL);

  /* Condition 3: We are in OAM and the STAT source bit is set */
  const bool cond_c = cur_mode == PPU::StatModes::MODE_OAM_SCAN &&
                      stat_.int_enabled(PPU::StatIntFlags::MODE_2_SEL);

  /* Condition 4: We are in VBLANK and the STAT source bit is set. For some
   * reason, this condition is also met in OAM scan as per TCAGBD. */
  const bool cond_d = cur_mode == PPU::StatModes::MODE_VBLANK &&
                      (stat_.int_enabled(PPU::StatIntFlags::MODE_2_SEL) ||
                       stat_.int_enabled(PPU::StatIntFlags::MODE_1_SEL));

  // Detect rising edge, fire IRQ appropriately
  stat_irq_signal_edge = cond_a || cond_b || cond_c || cond_d;
  if (!old && stat_irq_signal_edge)
    request_lcd_irq();
  stat_.set_ly_eq_lyc(ly_.peek() == lyc_.peek());
}

void PixelProcessingUnit::reset() {
  ppu_enable_oam_bug = true;
  flush_on_disable = true;
  stat_delay.clear();
  fetcher->reset();
  obj_fifo.flush();
  bg_fifo.flush();

  /* Configure FSM timing metadata */
  cur_scanline_clks = cur_mode_clks = 0;
  total_mode_clks = std::nullopt;

  /* Configure status MMIO register initial state. The state bits read zero
   * (HBLANK) when the PPU is disabled via bit zero of the LCDC register. */
  state = PPU::StatModes::MODE_OAM_SCAN; // PPU itself always starts in OAM SCAN
  stat_.set_mode(PPU::StatModes::MODE_HBLANK);
  ly_.reset();
}

void PixelProcessingUnit::step() {

  /* When the PPU is disabled, the screen just shows plain white and the state
   * is set to it's initial state until it is re-enabled again. */
  if (!lcdc_.lcd_enabled()) {
    do_disabled();
    return;
  } else
    flush_on_disable = true;

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
  stat_delay.push(state);
  if (stat_delay.full())
    update_stat(stat_delay.pop());
}

#undef DMG_COLOR_PRESERVE_HACK
