#include "ppu/fifo.hpp"
#include "memory/mmio/dmg.hpp"
#include "ppu/ppu.hpp"
#include <cstddef>
#include <optional>

PixelFifo::PixelFifo(PixelProcessingUnit &ppu)
    : fifo(CircularFifo<pixel, 16>()), ppu_(ppu) {
  total_clks = std::nullopt;
  cur_clks = 0;
  state = PixelFifoState::STATE_GET_TILE;

  /* Initialize fifo pixel fetcher */
  fetcher = {
      .tile_idx = 0,
      .data_lo = 0,
      .data_hi = 0,
      .x_coor = 0,
  };
}

/* We derive the Y-coordinate at a pixel level using the LY register. */
const byte_t PixelFifo::get_pixel_y() const {
  const byte_t scy = ppu_.scy_reg.read();
  const byte_t ly = ppu_.ly_reg.read();
  return (ly + scy) & 0xFF;
}

/* We derive the X-coordinate at a tile level, since the FIFO fetches eight
 * pixels at a time. As a result, we have to remember to divide the scroll
 * value by the size of a pixel to accomodate the change in units.  */
const byte_t PixelFifo::get_tile_x() const {
  constexpr byte_t pixels_per_row = 8;
  const byte_t scx = ppu_.scx_reg.read();
  const byte_t x = fetcher.x_coor;
  // Since returning unit tiles, can only be 32 max
  return (x + (scx / pixels_per_row)) & 0x1F;
}

/* Calculate which tile to read from based on tilemaps */
std::size_t PixelFifo::calc_tile_idx() const {
  constexpr auto tile_pixels = 8;
  constexpr auto tile_shift = 5;
  const byte_t y_pixel = get_pixel_y();

  // Calculate X and Y coordinates of tile
  const std::size_t y_tile = (y_pixel / tile_pixels);
  const std::size_t x_tile = get_tile_x();

  // Base address changes depending on LCDC bits being set
  const addr_t tile_idx = (y_tile << tile_shift) | x_tile;
  const addr_t tilemap_base = calc_tilemap_base();
  return ppu_.bus->read_byte(tilemap_base + tile_idx);
}

/* Calculate the base address of the tilemap for bg/win */
addr_t PixelFifo::calc_tilemap_base() const {
  const auto base_addr = ppu_.lcdc_reg.bg_tilemap_base();
  return static_cast<addr_t>(base_addr);
}

byte_t PixelFifo::fetch_tile_data(bool high) const {
  constexpr auto tile_size_bytes = 16;
  constexpr auto tile_row_bytes = 2;

  // Get the current Y coordinate at a pixel granularity
  const byte_t y_pixel_idx = get_pixel_y() & 0x7;
  const addr_t y_offset = y_pixel_idx * tile_row_bytes;

  // Need to consider y-offset based on LY register
  addr_t data_offset = (fetcher.tile_idx * tile_size_bytes) + y_offset;
  if (high)
    ++data_offset;

  // Read data based on bg/win data addressing mode
  switch (ppu_.lcdc_reg.bg_win_data_area()) {
  case PPU::TileDataArea::LO_TILEDATA_BASE:
    /* Inlined some math here, so if it's above 0x9000 you index it normally,
     * but if it is below you basically treat the tile offset like a 0-127
     * offset from 0x8800. You can just use 0x8800 - (127 * tile size in bytes)
     * to achieve the same effect, hence I deviate from the docs a bit. */
    return fetcher.tile_idx < 128 ? ppu_.bus->read_byte(0x9000 + data_offset)
                                  : ppu_.bus->read_byte(0x8000 + data_offset);
  case PPU::TileDataArea::HI_TILEDATA_BASE:
    return ppu_.bus->read_byte(0x8000 + data_offset);
  }
}

bool PixelFifo::should_discard() const {
  return fetcher.bg_discards < (ppu_.scx_reg.read() & 0x7);
}

void PixelFifo::get_tile() {
  constexpr std::size_t max_state_clks = 2;
  using modes = PixelFifo::PixelFifoState;

  // State entry logic - compute tile index
  if (!total_clks.has_value()) {
    fetcher.tile_idx = calc_tile_idx();
    total_clks = max_state_clks;
    fetcher.x_coor = (fetcher.x_coor + 1) & 0x1F;
  }
  ++cur_clks;

  // Get tile incomplete
  if (cur_clks >= total_clks.value()) {
    state = modes::STATE_GET_TILE_DATA_LOW;
    total_clks.reset();
    cur_clks = 0;
  }
}

void PixelFifo::get_tile_data_lo() {
  constexpr std::size_t max_state_clks = 2;
  using modes = PixelFifo::PixelFifoState;

  // State entry logic
  if (!total_clks.has_value()) {
    fetcher.data_lo = fetch_tile_data(false);
    total_clks = max_state_clks;
  }
  ++cur_clks;

  // Get tile incomplete
  if (cur_clks >= total_clks.value()) {
    state = modes::STATE_GET_TILE_DATA_HIGH;
    total_clks.reset();
    cur_clks = 0;
  }
}

void PixelFifo::get_tile_data_hi() {
  constexpr std::size_t max_state_clks = 2;
  using modes = PixelFifo::PixelFifoState;

  // State entry logic
  if (!total_clks.has_value()) {
    fetcher.data_hi = fetch_tile_data(true);
    total_clks = max_state_clks;
  }
  ++cur_clks;

  // Get tile incomplete
  if (cur_clks >= total_clks.value()) {
    state = modes::STATE_PUSH;
    total_clks.reset();
    cur_clks = 0;
  }
}

void PixelFifo::do_push() {
  using modes = PixelFifo::PixelFifoState;
  constexpr std::size_t min_state_clks = 2;
  constexpr auto pixels_per_row = 8;

  // State entry logic
  if (!total_clks.has_value())
    total_clks = min_state_clks;
  ++cur_clks;

  // This takes two clock cycles at best
  if (cur_clks < min_state_clks)
    return;

  // Eight pixels are pushed at a time, must have room
  if (fifo.size() > fifo.capacity() - pixels_per_row)
    return;

  // Compute palette indices
  for (int shift{7}; shift >= 0; shift--) {
    const byte_t hi_bit = (fetcher.data_hi & (1 << shift)) != 0 ? 1 : 0;
    const byte_t lo_bit = (fetcher.data_lo & (1 << shift)) != 0 ? 1 : 0;
    const byte_t palette_idx = (hi_bit << 1) | lo_bit;
    const bool discard = should_discard();

    // Track number of pixels discarded
    if (discard)
      ++fetcher.bg_discards;

    /* We still push the pixel into the FIFO, the PPU makes the final call on
     * whether to render it or not. If `discard` is `true`, it will pop the
     * pixel off and ignore it. */
    fifo.push({
        .color = palette_idx, // TODO: Fix coloring
        .discard = discard,
    });
  }

  // State transition after push to fetch next row
  state = modes::STATE_GET_TILE;
  total_clks.reset();
  cur_clks = 0;
}

void PixelFifo::reset() {
  using modes = PixelFifo::PixelFifoState;
  byte_t fine_scroll = ppu_.scx_reg.read() & 0x7;
  state = modes::STATE_GET_TILE;

  /* Reset internal timing and state info */
  fetcher = {
      .tile_idx = 0,
      .data_lo = 0,
      .data_hi = 0,
      .x_coor = 0,
      .x_fine_scroll = fine_scroll,
      .bg_discards = 0,
  };
  total_clks.reset();
  cur_clks = 0;
  fifo.clear();
}

void PixelFifo::step() {
  switch (state) {
  case STATE_GET_TILE:
    get_tile();
    break;
  case STATE_GET_TILE_DATA_LOW:
    get_tile_data_lo();
    break;
  case STATE_GET_TILE_DATA_HIGH:
    get_tile_data_hi();
    break;
  case STATE_PUSH:
    do_push();
    break;
  }
}
