#include "ppu/fifo.hpp"
#include "ppu/ppu.hpp"
#include <cstddef>
#include <optional>

PixelFifo::PixelFifo(PixelProcessor *ppu_ptr)
    : fifo(CircularFifo<pixel, 16>()), ppu(ppu_ptr) {
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

/* Calculate which tile to read from based on tilemaps */
std::size_t PixelFifo::calc_tile_idx() const {
  constexpr auto tile_mask = 0x1F; // Maximum value of 31
  constexpr auto tile_pixels = 8;
  constexpr auto tile_shift = 5;

  // Calculate X and Y coordinates of tile
  const std::size_t y_tile = (ppu->ly_reg->read() / tile_pixels) & tile_mask;
  const std::size_t x_tile = fetcher.x_coor & tile_mask; // Tile

  // TODO: Consider configurable indexing modes
  const addr_t tile_idx = (y_tile << tile_shift) | x_tile;
  constexpr auto tilemap_base = 0x9800;
  return ppu->bus->read_byte(tilemap_base | tile_idx);
}

byte_t PixelFifo::fetch_tile_data(bool high) const {
  constexpr auto vram_base_addr = 0x8000;
  constexpr auto tile_size_bytes = 16;
  constexpr auto tile_row_bytes = 2;

  // Get the current Y coordinate at a pixel granularity
  const byte_t y_pixel_idx = ppu->ly_reg->read() & 0x7;
  const addr_t y_offset = y_pixel_idx * tile_row_bytes;

  // Need to consider y-offset based on LY register
  const addr_t tile_base_addr = fetcher.tile_idx * tile_size_bytes;
  addr_t data_addr = vram_base_addr | tile_base_addr | y_offset;
  if (high)
    ++data_addr;
  return ppu->bus->read_byte(data_addr);
}

void PixelFifo::get_tile() {
  constexpr std::size_t max_state_clks = 2;
  using modes = PixelFifo::PixelFifoState;
  constexpr auto x_coor_mask = 0x1F;

  // State entry logic - compute tile index
  if (!total_clks.has_value()) {
    fetcher.tile_idx = calc_tile_idx();
    total_clks = max_state_clks;

    // Advance the state of the fetcher, always < 32
    fetcher.x_coor = (fetcher.x_coor + 1) & x_coor_mask;
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
    state = modes::STATE_SLEEP;
    total_clks.reset();
    cur_clks = 0;
  }
}

void PixelFifo::sleep() {
  constexpr std::size_t max_state_clks = 2;
  using modes = PixelFifo::PixelFifoState;

  // State entry logic
  if (!total_clks.has_value()) {
    total_clks = max_state_clks;
  }
  ++cur_clks;

  // Get tile incomplete
  if (cur_clks >= total_clks.value()) {
    state = modes::STATE_SLEEP;
    total_clks.reset();
    cur_clks = 0;
  }
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
  case STATE_SLEEP:
    sleep();
    break;
  }
}
