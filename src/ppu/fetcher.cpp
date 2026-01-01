#include "ppu/fetcher.hpp"
#include "memory/bus.hpp"
#include "ppu/fifo.hpp"

Fetcher::Fetcher(AddressBus *const bus_ptr, PixelFifo &fifo, PPU::LCDCtrl &lcdc,
                 MMIORegister &scx, MMIORegister &scy, PPU::LY &ly)
    : bus(bus_ptr), // For reading tile data from VRAM
      fifo_(fifo),  // Pixel fifo
      lcdc_(lcdc),  // LCD control register
      scx_(scx),    // Scroll X (background)
      scy_(scy),    // Scroll Y (background)
      ly_(ly)       // Current scanline
{
  reset();
}

void Fetcher::reset() {
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

  // Reset timing metadata
  total_clks.reset();
  cur_clks = 0;
}

/* We derive the Y-coordinate at a pixel level using the LY register. */
const byte_t Fetcher::calc_pixel_y() const {
  return (ly_.read() + scy_.read()) & 0xFF;
}

/* We derive the X-coordinate at a tile level, since the FIFO fetches eight
 * pixels at a time. As a result, we have to remember to divide the scroll
 * value by the size of a pixel to accomodate the change in units.  */
const byte_t Fetcher::calc_tile_x() const {
  constexpr byte_t pixels_per_row = 8;
  // Since returning unit tiles, can only be 32 max
  return (data.x_coor + (scx_.read() / pixels_per_row)) & 0x1F;
}

const addr_t Fetcher::calc_tilemap_base() const {
  const auto base_addr = lcdc_.bg_tilemap_base();
  return static_cast<addr_t>(base_addr);
}

std::size_t Fetcher::calc_tile_idx() {
  constexpr auto row_pixels = 8;
  constexpr auto tile_shift = 5;
  const byte_t y_px = calc_pixel_y();

  // Calculate X and Y coordinates of tile
  const std::size_t y_tile = (y_px / row_pixels);
  const std::size_t x_tile = calc_tile_x();

  // Base address changes depending on LCDC bits being set
  const addr_t tile_idx = (y_tile << tile_shift) | x_tile;
  const addr_t tilemap_base = calc_tilemap_base();
  return bus->read_byte(tilemap_base + tile_idx);
}

bool Fetcher::should_discard() const {
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
    return data.tile_idx < 128 ? bus->read_byte(0x9000 + data_offset)
                               : bus->read_byte(0x8000 + data_offset);
  case PPU::TileDataArea::HI_TILEDATA_BASE:
    return bus->read_byte(0x8000 + data_offset);
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
    const byte_t hi_bit = (data.data_hi & (1 << shift)) != 0 ? 1 : 0;
    const byte_t lo_bit = (data.data_lo & (1 << shift)) != 0 ? 1 : 0;
    const byte_t palette_idx = (hi_bit << 1) | lo_bit;
    const bool discard = should_discard();

    // Track number of pixels discarded to implement fine scroll
    if (discard)
      ++pixels_discarded;

    pixel const px = {
        .color = palette_idx,
        .discard = discard,
    };
    fifo_.push(px);
  }
  data.x_coor = (data.x_coor + 1) & 0x1F;

  // State transition after push to fetch next row
  state = STATE_READ_TILE;
  total_clks.reset();
  cur_clks = 0;
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
