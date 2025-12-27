#include "ppu/fifo.hpp"
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

const byte_t PixelFifo::get_pixel_y() const {
  const byte_t scy = ppu_.scy_reg.read();
  const byte_t ly = ppu_.ly_reg.read();
  return (ly + scy) & 0xFF;
}

const byte_t PixelFifo::get_tile_x() const {
  const byte_t scx = ppu_.scx_reg.read();
  const byte_t x = fetcher.x_coor;
  return (x + scx) & 0xFF;
}

/* Calculate which tile to read from based on tilemaps */
std::size_t PixelFifo::calc_tile_idx() const {
  constexpr auto tile_pixels = 8;
  constexpr auto tile_shift = 5;
  const byte_t y_pixel = get_pixel_y();

  // Calculate X and Y coordinates of tile
  const std::size_t y_tile = (y_pixel / tile_pixels);
  const std::size_t x_tile = get_tile_x();

  // TODO: Consider configurable indexing modes
  const addr_t tile_idx = (y_tile << tile_shift) | x_tile;
  constexpr auto tilemap_base = 0x9800;
  return ppu_.bus->read_byte(tilemap_base + tile_idx);
}

byte_t PixelFifo::fetch_tile_data(bool high) const {
  constexpr auto vram_base_addr = 0x8000;
  constexpr auto tile_size_bytes = 16;
  constexpr auto tile_row_bytes = 2;

  // Get the current Y coordinate at a pixel granularity
  const byte_t y_pixel_idx = get_pixel_y() & 0x7;
  const addr_t y_offset = y_pixel_idx * tile_row_bytes;

  // Need to consider y-offset based on LY register
  const addr_t tile_base_addr = fetcher.tile_idx * tile_size_bytes;
  addr_t data_addr = vram_base_addr + tile_base_addr + y_offset;
  if (high)
    ++data_addr;
  return ppu_.bus->read_byte(data_addr);
}

void PixelFifo::get_tile() {
  constexpr std::size_t max_state_clks = 2;
  using modes = PixelFifo::PixelFifoState;

  // State entry logic - compute tile index
  if (!total_clks.has_value()) {
    fetcher.tile_idx = calc_tile_idx();
    total_clks = max_state_clks;

    // Advance the state of the fetcher, always < 20
    if (++fetcher.x_coor >= 20)
      fetcher.x_coor = 0;
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

    // TODO: Index palette, just pushing the index for now
    fifo.push({.color = palette_idx});
  }

  // State transition after push to fetch next row
  state = modes::STATE_GET_TILE;
  total_clks.reset();
  cur_clks = 0;
}

void PixelFifo::reset() {
  using modes = PixelFifo::PixelFifoState;
  state = modes::STATE_GET_TILE;

  /* Reset internal timing and state info */
  fetcher.x_coor = 0;
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
