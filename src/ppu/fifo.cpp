#include "ppu/fifo.hpp"
#include <cstddef>
#include <optional>

PixelFifo::PixelFifo(PixelProcessor *ppu_ptr)
    : fifo(CircularFifo<pixel, 16>()), ppu(ppu_ptr) {
  total_clks = std::nullopt;
  cur_clks = 0;
  state = PixelFifoState::STATE_GET_TILE;
}

void PixelFifo::get_tile() {
  constexpr std::size_t max_state_clks = 2;
  using modes = PixelFifo::PixelFifoState;

  // State entry logic
  if (!total_clks.has_value()) {
    total_clks = max_state_clks;
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
