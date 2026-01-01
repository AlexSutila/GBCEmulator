#include "ppu/fetcher.hpp"
#include "cart/cart.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/fifo.hpp"

#include <array>
#include <cassert>
#include <memory>
#include <stdexcept>

constexpr addr_t vram_base_addr = 0x8000;
constexpr byte_t pixels_per_row = 8;

Fetcher::Fetcher(std::array<std::unique_ptr<byte_t[]>, 2> &vram,
                 PPU::VramBank *const vbk_ptr, PPU::LCDCtrl &lcdc,
                 MMIORegister &scy, MMIORegister &scx, MMIORegister &wy,
                 MMIORegister &wx, PPU::LY &ly, PixelFifo &fifo)
    : vram_(vram),   // For fetching tile data
      vbk_(vbk_ptr), // Tile data is banked in CGB mode
      lcdc_(lcdc),   // LCD control register
      scy_(scy),     // Scroll Y (background)
      scx_(scx),     // Scroll X (background)
      wy_(wy),       // Window  Y (background)
      wx_(wx),       // Window  X (background)
      ly_(ly),       // Current scanline
      fifo_(fifo)    // Pixel fifo
{
  reset();
}

void Fetcher::set_cgb(const byte_t cgb_flag) { is_cgb = cgb_enabled(cgb_flag); }

void Fetcher::reset(bool window_started) {
  fine_scroll = scx_.read() & 0x7;
  state = STATE_READ_TILE;
  pixels_discarded = 0;

  // Reset fetcher data, x_coor most important
  data = {
      .tile_idx = 0,
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
  bool win_visible = is_window_visible(0);
  reset(win_visible);
}

// The fetcher may need to read from banks which are not currently active to
// fetch specific tile metadata (CGB mode BG map attributes, for example).
byte_t Fetcher::read_vram_byte(addr_t addr, byte_t bank) const {
  assert((addr >= 0x8000 && addr <= 0x9FFF) && (bank < 2));
  if (!is_cgb && bank != 0)
    throw std::runtime_error(
        "Fetcher::read_vram_byte(), non-zero bank in DMG mode");
  return vram_.at(bank)[addr - vram_base_addr];
}
byte_t Fetcher::read_vram_byte(addr_t addr) const {
  const byte_t vram_bank = is_cgb ? vbk_->read() : 0;
  return read_vram_byte(addr, vram_bank);
}

/* We derive the Y-coordinate at a pixel level using the LY register. */
const byte_t Fetcher::calc_pixel_y() const {
  if (win_started)
    return win_internal_ly & 0xFF;
  return (ly_.read() + scy_.read()) & 0xFF;
}

/* We derive the X-coordinate at a tile level, since the FIFO fetches eight
 * pixels at a time. As a result, we have to remember to divide the scroll
 * value by the size of a pixel to accomodate the change in units.  */
const byte_t Fetcher::calc_tile_x() const {
  if (win_started)
    return data.x_coor & 0x1F;
  // Since returning unit tiles, can only be 32 max
  return (data.x_coor + (scx_.read() / pixels_per_row)) & 0x1F;
}

const addr_t Fetcher::calc_tilemap_base() const {
  const auto base_addr =
      win_started ? lcdc_.win_tilemap_base() : lcdc_.bg_tilemap_base();
  return static_cast<addr_t>(base_addr);
}

std::size_t Fetcher::calc_tile_idx() {
  constexpr auto tile_shift = 5;
  const byte_t y_px = calc_pixel_y();

  // Calculate X and Y coordinates of tile
  const std::size_t y_tile = (y_px / pixels_per_row);
  const std::size_t x_tile = calc_tile_x();

  // Base address changes depending on LCDC bits being set
  const addr_t tile_idx = (y_tile << tile_shift) | x_tile;
  const addr_t tilemap_base = calc_tilemap_base();

  // Tilemap indices are ALWAYS read from bank zero
  return read_vram_byte(tilemap_base + tile_idx, 0);
}

/* Implements fine horizontal scroll and initial tile skip */
bool Fetcher::should_discard() const {
  if (win_started)
    return false;
  return pixels_discarded < fine_scroll;
}

/* Calculate the base address of the tilemap for bg/win */
const byte_t Fetcher::fetch_tile_data(bool high) const {
  constexpr auto tile_size_bytes = 16;
  constexpr auto tile_row_bytes = 2;

  // Get the current Y coordinate at a pixel granularity
  const byte_t y_pixel_idx = calc_pixel_y() & 0x7;
  const addr_t y_offset = y_pixel_idx * tile_row_bytes;

  // Need to consider y-offset based on LY register
  addr_t data_offset = (data.tile_idx * tile_size_bytes) + y_offset;
  if (high)
    ++data_offset;

  // Read data based on bg/win data addressing mode
  switch (lcdc_.bg_win_data_area()) {
  case PPU::TileDataArea::LO_TILEDATA_BASE:
    /* Inlined some math here, so if it's above 0x9000 you index it normally,
     * but if it is below you basically treat the tile offset like a 0-127
     * offset from 0x8800. You can just use 0x8800 - (127 * tile size in bytes)
     * to achieve the same effect, hence I deviate from the docs a bit. */
    return data.tile_idx < 128 ? read_vram_byte(0x9000 + data_offset)
                               : read_vram_byte(0x8000 + data_offset);
  case PPU::TileDataArea::HI_TILEDATA_BASE:
    /* The calculation here is much more straight forward, simple offset. */
    return read_vram_byte(0x8000 + data_offset);
  default:
    throw std::runtime_error(
        "Fetcher::fetch_tile_data(), Invalid tilemap addressing mode");
  }
}

void Fetcher::do_read_tile() {
  constexpr std::size_t max_state_clks = 2;

  // State entry logic
  if (!total_clks.has_value()) {
    data.tile_idx = calc_tile_idx();
    total_clks = max_state_clks;
  }
  ++cur_clks;

  // Read tile incomplete
  if (cur_clks >= total_clks.value()) {
    state = STATE_READ_DATA_LO;
    total_clks.reset();
    cur_clks = 0;
  }
}

void Fetcher::do_read_data_lo() {
  constexpr std::size_t max_state_clks = 2;

  // State entry logic, false indicates low byte
  if (!total_clks.has_value()) {
    data.data_lo = fetch_tile_data(false);
    total_clks = max_state_clks;
  }
  ++cur_clks;

  // Read tile incomplete
  if (cur_clks >= total_clks.value()) {
    state = STATE_READ_DATA_HI;
    total_clks.reset();
    cur_clks = 0;
  }
}

void Fetcher::do_read_data_hi() {
  constexpr std::size_t max_state_clks = 2;

  // State entry logic, false indicates high byte
  if (!total_clks.has_value()) {
    data.data_hi = fetch_tile_data(true);
    total_clks = max_state_clks;
  }
  ++cur_clks;

  // Read tile incomplete
  if (cur_clks >= total_clks.value()) {
    state = STATE_PUSH_DATA;
    total_clks.reset();
    cur_clks = 0;
  }
}

void Fetcher::do_push_data() {
  constexpr std::size_t min_state_clks = 2;

  // State entry logic
  if (!total_clks.has_value())
    total_clks = min_state_clks;
  ++cur_clks;

  // This takes two clock cycles at best, takes longer if fifo is packed
  if (cur_clks < min_state_clks || !fifo_.can_push())
    return;

  // Compute palette indices
  for (int shift{7}; shift >= 0; shift--) {
    const bool discard = should_discard();

    // Track number of pixels discarded to implement fine scroll
    if (discard)
      ++pixels_discarded;

    // Otherwise, we compute the pixel info as you would usually
    const byte_t hi_bit = (data.data_hi & (1 << shift)) != 0 ? 1 : 0;
    const byte_t lo_bit = (data.data_lo & (1 << shift)) != 0 ? 1 : 0;
    byte_t palette_idx = (hi_bit << 1) | lo_bit;

    fifo_.push({
        .color = palette_idx,
        .discard = discard,
    });
  }
  data.x_coor = (data.x_coor + 1) & 0x1F;

  // State transition after push to fetch next row
  state = STATE_READ_TILE;
  total_clks.reset();
  cur_clks = 0;
}

bool Fetcher::is_window_visible(byte_t pixels_rendered) const {
  if (!lcdc_.win_enabled())
    return false;
  const byte_t wx_px = wx_.read();
  const byte_t wy_px = wy_.read();
  const byte_t ly_px = ly_.read();
  return (wx_px <= pixels_rendered + 7) && (ly_px >= wy_px);
}

void Fetcher::render_window() {
  // Window is already being rendered
  if (win_started)
    return;

  // Flush BG fifo pixel data, incurs additional overhead to fetch the very
  // first window tile, but after that the rendering process is identical.
  fifo_.flush();
  reset(true);
}

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
  }
}
