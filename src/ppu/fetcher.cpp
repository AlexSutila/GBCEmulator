#include "ppu/fetcher.hpp"
#include "gbc.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/attributes.hpp"
#include "ppu/fifo.hpp"
#include "ppu/pixel.hpp"

#include <array>
#include <cassert>
#include <memory>
#include <stdexcept>

constexpr addr_t vram_base_addr = 0x8000;
constexpr byte_t pixels_per_row = 8;

Fetcher::Fetcher(std::array<std::unique_ptr<byte_t[]>, 2> &vram,
                 PPU::LCDCtrl &lcdc, MMIORegister &scy, MMIORegister &scx,
                 MMIORegister &wy, MMIORegister &wx, PPU::OPRI &opri,
                 PPU::LY &ly, ObjPixelFifo &obj_fifo, BgPixelFifo &bg_fifo,
                 runtime_sys_info &sys)
    : vram_(vram),         // For fetching tile data
      lcdc_(lcdc),         // LCD control register
      scy_(scy),           // Scroll Y (background)
      scx_(scx),           // Scroll X (background)
      wy_(wy),             // Window  Y (background)
      wx_(wx),             // Window  X (background)
      opri_(opri),         // CGB object priority
      ly_(ly),             // Current scanline
      obj_fifo_(obj_fifo), // Sprite pixel fifo
      bg_fifo_(bg_fifo),   // Background pixel fifo
      sys_(sys)            // Behavior varies with DMG vs CGB
{
  reset();
}

void Fetcher::reset(const bool window_started) {
  coarse_scroll_x = scx_.peek();
  fine_scroll_x = coarse_scroll_x & 0x7;
  fine_scroll_y = scy_.peek();
  state = STATE_READ_TILE;
  pixels_discarded = 0;

  // Reset fetcher data, x_coor most important
  data = {
      .tile_idx = 0,
      .tile_attr = 0,
      .data_lo = 0,
      .data_hi = 0,
      .x_coor = 0,
  };
  win_started = window_started;

  // Reset timing metadata
  total_clks.reset();
  cur_clks = 0;
}

// Always enters background rendering mode, unless window is rendered instantly
void Fetcher::reset() {
  const bool win_visible = is_window_visible(0);
  reset(win_visible);
}

bool Fetcher::is_window_visible(byte_t pixels_rendered) const {
  if (!win_enable_sample)
    return false;
  const byte_t wx_px = wx_.peek();
  const byte_t wy_px = wy_.peek();
  const byte_t ly_px = ly_.peek();
  return (wx_px <= pixels_rendered + 7) && (ly_px >= wy_px);
}

void Fetcher::sample_window_enable() {
  win_enable_sample = lcdc_.win_enabled();
}

void Fetcher::render_window() {
  // Window is already being rendered. Also, if a sprite fetch is in progress,
  // do not interrupt it. This will take effect afterward.
  if (win_started || state == STATE_SPRITE_FETCH)
    return;

  // Flush BG fifo pixel data, incurs additional overhead to fetch the very
  // first window tile, but after that the rendering process is identical.
  bg_fifo_.flush();
  reset(true);
}

// The fetcher may need to read from banks which are not currently active to
// fetch specific tile metadata (CGB mode BG map attributes, for example).
byte_t Fetcher::read_vram_byte(const addr_t addr, const byte_t bank) const {
  assert((addr >= 0x8000 && addr <= 0x9FFF) && (bank < 2));
  return vram_.at(bank)[addr - vram_base_addr];
}

byte_t Fetcher::calc_bgwin_pixel_y() const {
  if (win_started)
    return win_internal_ly & 0xFF;
  return (ly_.peek() + fine_scroll_y) & 0xFF;
}

byte_t Fetcher::calc_bgwin_tile_x() const {
  if (win_started)
    return data.x_coor & 0x1F;
  // Since returning unit tiles, can only be 32 max
  return (data.x_coor + (coarse_scroll_x / pixels_per_row)) & 0x1F;
}

byte_t Fetcher::calc_obj_pixel_y(const Sprite &sprite) const {
  return (ly_.peek() - (sprite.y_pos - 16)) & 0xFF;
}

addr_t Fetcher::calc_tilemap_base() const {
  const auto base_addr =
      win_started ? lcdc_.win_tilemap_base() : lcdc_.bg_tilemap_base();
  return static_cast<addr_t>(base_addr);
}

/* For any given tile, the bytes which represent the index into the tilemap and
 * the tile attributes actually lie at the same address. The difference between
 * the physical locations of both bytes is which bank they lie in. Hence, we can
 * leverage the same address calculation for tile indices and attributes. */
addr_t Fetcher::calc_tile_metadata_addr() const {
  constexpr auto tile_shift = 5;
  const byte_t y_px = calc_bgwin_pixel_y();

  // Calculate X and Y coordinates of tile
  const std::size_t y_tile = (y_px / pixels_per_row);
  const std::size_t x_tile = calc_bgwin_tile_x();

  // Base address changes depending on LCDC bits being set
  const addr_t tile_idx = (y_tile << tile_shift) | x_tile;
  const addr_t tilemap_base = calc_tilemap_base();
  return tilemap_base + tile_idx;
}

/* In DMG mode, the priority is always resolved by simply prioritizing the one
 * sprite which appears earliest in the scanline based on x-pos. In other words
 * we only write over 'transparent' pixels in the FIFO. In CGB mode, this can be
 * done as well, but you can also choose based on OAM index optionally. */
bool Fetcher::has_priority(const pixel &old_px, const byte_t new_oam_idx,
                           const byte_t new_color_idx) const {
  using prioMode = PPU::ObjectPriorityMode;
  const prioMode prio = opri_.get_prio_mode();
  const bool old_transparent = is_transparent(old_px);

  // Rule 1: new pixel transparent -> never wins
  if (is_transparent(new_color_idx))
    return false;

  // Rule 2: old pixel transparent -> always wins
  if (old_transparent)
    return true;

  // Rule 3: DMG or DMG-style priority
  if (!sys_.cgb_mode || prio == prioMode::OPRI_DMG_STYLE)
    return false;

  // Rule 4: CGB priority -> lower OAM index wins
  return new_oam_idx < old_px.oam_index;
}

byte_t Fetcher::calc_sprite_tile_idx(const Sprite &sprite) const {
  if (const bool tall = lcdc_.obj_size() == PPU::SpriteHeight::TALL_SPRITES;
      !tall)
    // Regular 8x8 sprites do not have their LSB set by hardware
    return sprite.tile_idx;

  // But tall sprites do to make up for the fact that the total number of
  // sprites is cut in half since sprite require two tiles each.
  return sprite.tile_idx & ~0x1;
}

/* Implements fine horizontal scroll and initial tile skip */
bool Fetcher::should_discard() const {
  if (win_started)
    return false;
  return pixels_discarded < fine_scroll_x;
}

/* Calculate the base address of the tilemap for bg/win */
byte_t Fetcher::fetch_bgwin_tile_data(const bool high) const {
  constexpr auto tile_size_bytes = 16;
  constexpr auto tile_row_bytes = 2;
  const byte_t y_px_idx = calc_bgwin_pixel_y();

  // Get the current Y coordinate at a pixel granularity
  const bool flip = get_bg_attrib_y_flip(data.tile_attr);
  const byte_t y_px_idx_flipped = do_y_px_flip(y_px_idx, flip, false);
  const addr_t y_offset = y_px_idx_flipped * tile_row_bytes;

  // Need to consider y-offset based on LY register
  addr_t data_offset = (data.tile_idx * tile_size_bytes) + y_offset;
  if (high)
    ++data_offset;

  // If we are in CGB mode, the tile data can come from either VRAM bank. The
  // bank to fetch the tile from comes from the tile attributes. When in DMG
  // mode, the lower bank is always used.
  const byte_t bank = get_bg_attrib_bank(data.tile_attr);

  // Read data based on bg/win data addressing mode
  switch (lcdc_.bg_win_data_area()) {
  case PPU::TileDataArea::LO_TILEDATA_BASE:
    /* Inlined some math here, so if it's above 0x9000 you index it normally,
     * but if it is below you basically treat the tile offset like a 0-127
     * offset from 0x8800. You can just use 0x8800 - (128 * tile size in bytes)
     * to achieve the same effect, hence I deviate from the docs a bit. */
    return data.tile_idx < 128 ? read_vram_byte(0x9000 + data_offset, bank)
                               : read_vram_byte(0x8000 + data_offset, bank);
  case PPU::TileDataArea::HI_TILEDATA_BASE:
    /* The calculation here is much more straight forward, simple offset. */
    return read_vram_byte(0x8000 + data_offset, bank);
  default:
    throw std::runtime_error(
        "BgWinFetcher::fetch_tile_data(), Invalid tilemap addressing mode");
  }
}

byte_t Fetcher::fetch_obj_tile_data(const Sprite &sprite,
                                    const bool high) const {
  constexpr auto tile_size_bytes = 16;
  constexpr auto tile_row_bytes = 2;
  const byte_t y_px_idx = calc_obj_pixel_y(sprite);
  const bool tall = lcdc_.obj_size() == PPU::SpriteHeight::TALL_SPRITES;
  const bool flip = get_obj_attrib_y_flip(sprite.tile_attr);

  // Get the current Y coordinate at a pixel granularity
  const byte_t y_px_idx_flipped = do_y_px_flip(y_px_idx, flip, tall);
  const addr_t y_offset = y_px_idx_flipped * tile_row_bytes;
  const byte_t tile_idx = calc_sprite_tile_idx(sprite);

  // Need to consider y-offset based on LY register
  addr_t data_offset = (tile_idx * tile_size_bytes) + y_offset;
  if (high)
    ++data_offset;

  // If we are in CGB mode, we have the option to use the high or low bank
  // based on the attribute flags. Otherwise, we always use zero in DMG.
  const byte_t bank = sys_.cgb_mode ? get_obj_attrib_bank(sprite.tile_attr) : 0;
  return read_vram_byte(0x8000 + data_offset, bank); // Always 0x8000
}

void Fetcher::do_read_tile() {
  // State entry logic
  if (!total_clks.has_value()) {
    constexpr std::size_t max_state_clks = 2;
    const addr_t metadata_addr = calc_tile_metadata_addr();
    data.tile_idx = read_vram_byte(metadata_addr, 0);
    // Tile attributes are only fetched in CGB mode, I do not think this
    // impacts the clock cycle duration of the initial fetching state.
    if (sys_.cgb_mode)
      data.tile_attr = read_vram_byte(metadata_addr, 1);
    total_clks = max_state_clks;
  }
  ++cur_clks;

  // Read tile complete
  if (cur_clks >= total_clks.value()) {
    state = STATE_READ_DATA_LO;
    total_clks.reset();
    cur_clks = 0;
  }
}

void Fetcher::do_read_data_lo() {
  // State entry logic, false indicates low byte
  if (!total_clks.has_value()) {
    constexpr std::size_t max_state_clks = 2;
    data.data_lo = fetch_bgwin_tile_data(false);
    total_clks = max_state_clks;
  }
  ++cur_clks;

  // Read tile complete
  if (cur_clks >= total_clks.value()) {
    state = STATE_READ_DATA_HI;
    total_clks.reset();
    cur_clks = 0;
  }
}

void Fetcher::do_read_data_hi() {
  // State entry logic, false indicates high byte
  if (!total_clks.has_value()) {
    constexpr std::size_t max_state_clks = 2;
    data.data_hi = fetch_bgwin_tile_data(true);
    total_clks = max_state_clks;
  }
  ++cur_clks;

  // Read tile complete
  if (cur_clks >= total_clks.value()) {
    state = STATE_PUSH_DATA;
    total_clks.reset();
    cur_clks = 0;
  }
}

void Fetcher::do_push_data() {
  if (!total_clks.has_value()) {
    constexpr std::size_t min_state_clks = 2;
    const bool take_priority = get_bg_attrib_priority(data.tile_attr);
    const byte_t palette_idx = get_bg_attrib_palette(data.tile_attr);
    const bool flip = get_bg_attrib_x_flip(data.tile_attr);

    // Attempt to push pixels to the FIFO, eight are pushed per push operation
    if (bg_fifo_.can_push()) {
      for (std::size_t shift{0}; shift < 8; shift++) {
        const bool discard = should_discard();
        if (discard)
          ++pixels_discarded;

        // If we are rendering the background, we should only push a pixel if
        // the background enable bit is set. Otherwise, just show color zero.
        const byte_t color_idx =
            calc_color_idx(data.data_lo, data.data_hi, shift, flip);
        if (!discard)
          bg_fifo_.push({
              .color_idx = color_idx,
              .palette_idx = palette_idx,
              .oam_index = 0, // Unused by the background
              .take_priority = take_priority,
          });
      }
      data.x_coor = (data.x_coor + 1) & 0x1F;
    }
    // Setup timing after pushing the pixels, gets us to 174 clocks minimum
    total_clks = min_state_clks;
  }
  ++cur_clks;

  // State transition after push to fetch next row
  if (cur_clks >= total_clks.value()) {
    state = STATE_READ_TILE;
    total_clks.reset();
    cur_clks = 0;
  }
}

// TODO: Ideally, this won't happen all in a single clock cycle. This is also
// preventing us from implementing sprite fetch cancelling. Research timings.
bool Fetcher::do_sprite_fetch(const Sprite &sprite) {
  constexpr std::size_t max_state_clks = 6;
  if (!total_clks.has_value())
    total_clks = max_state_clks;
  ++cur_clks;

  // Sprite fetch incomplete
  if (cur_clks < total_clks.value())
    return false;

  // Fetch tile data, both low and high bytes, from VRAM
  const byte_t data_lo = fetch_obj_tile_data(sprite, false);
  const byte_t data_hi = fetch_obj_tile_data(sprite, true);
  const byte_t oam_idx = sprite.obj_no;

  // Lastly, determine the sprite attributes needed for rendering. Keep in mind
  // that a cleared priority bit is what gives sprites higher priority over the
  // background and window, not a set bit. This is NOT THE SAME as object prio!
  const bool take_priority = get_obj_attrib_priority(sprite.tile_attr);
  const byte_t palette_idx = sys_.cgb_mode
                                 ? get_obj_attrib_cgb_palette(sprite.tile_attr)
                                 : get_obj_attrib_dmg_palette(sprite.tile_attr);
  const bool flip = get_obj_attrib_x_flip(sprite.tile_attr);
  obj_fifo_.fill_transparent();

  // To handle sprites clipping with the left side of the screen, we introduce
  // the `fifo_idx` to determine which pixel in the object fifo to "poke". If
  // the pixel is off-screen, we skip it, and only start emplacing pixels as
  // they actually become visible. TODO: This is likely not accurate.
  for (std::size_t shift{0}, fifo_idx{0}; shift < 8; shift++) {
    if (sprite.x_pos + shift < 8)
      continue;
    pixel &cur_px = obj_fifo_.at(fifo_idx);
    fifo_idx++;

    // Color idx calculation needs to consider horizontal flip attribute bit
    const byte_t color_idx = calc_color_idx(data_lo, data_hi, shift, flip);
    // Implements the logic behind the object pixel priority resolution. If the
    // new pixel has priority over the old one, the data is simply updated in
    // place. This is why we fill the FIFO with transparent pixels before doing
    // any pushes. The pandocs is wrong as well, OBJ FIFO is only 8 pixels wide.
    if (has_priority(cur_px, oam_idx, color_idx))
      cur_px = {
          .color_idx = color_idx,
          .palette_idx = palette_idx,
          .oam_index = oam_idx,
          .take_priority = take_priority,
      };
  }

  // Sprite fetch complete
  state = STATE_READ_TILE;
  total_clks.reset();
  cur_clks = 0;
  return true;
}

/* This is only to be called when one of the two conditions hold ---------- *
 *  1. We are waiting for a sprite fetch to start, no pixels should be popped
 *  2. A sprite fetch has started, and we are waiting for it to complete */
bool Fetcher::step_and_try_sprite_fetch(const Sprite &sprite) {
  /* Preempt the next background or window tile fetch, if possible. This will
   * only preempt if the ongoing background or window tile fetch is done. */
  if (state == STATE_READ_TILE && !total_clks.has_value())
    state = STATE_SPRITE_FETCH;

  /* If we have started performing a sprite fetch, step it until completion.
   * Return true once it is complete and the sprite data is emplaced in the
   * corresponding object FIFO. */
  if (state == STATE_SPRITE_FETCH)
    return do_sprite_fetch(sprite);

  /* Otherwise, we continue fetching from the BG or window as normal. This
   * happens until the next fetch is preempted, then we can perform a sprite
   * fetch. Hence, this wait should last no more than five clock cycles. */
  step();
  return false;
}

/* Call when doing any regular BG/WIN fetches. If a sprite is in the midst of
 * being rendered, this can (and should) be called safely. */
void Fetcher::step() {
  switch (state) {
  case STATE_READ_TILE:
    do_read_tile();
    break;
  case STATE_READ_DATA_LO:
    do_read_data_lo();
    break;
  case STATE_READ_DATA_HI:
    do_read_data_hi();
    break;
  case STATE_PUSH_DATA:
    do_push_data();
    break;
  default:
    throw std::runtime_error("Fetcher::step() bad state");
  }
}
