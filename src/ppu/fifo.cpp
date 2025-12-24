#include "ppu/fifo.hpp"
#include <optional>

PixelFifo::PixelFifo() : fifo(CircularFifo<pixel, 16>()) {
  cur_clks = std::nullopt;
  max_clks = 0;
  state = PixelFifoState::STATE_GET_TILE;
}

void PixelFifo::get_tile() {}

void PixelFifo::get_tile_data() {}

void PixelFifo::sleep() {}

void PixelFifo::step() {
  switch (state) {
  case STATE_GET_TILE:
    get_tile();
    break;
  case STATE_GET_TILE_DATA_LOW:
  case STATE_GET_TILE_DATA_HIGH:
    get_tile_data();
    break;
  case STATE_SLEEP:
    sleep();
    break;
  case STATE_PUSH:
    break;
  }
}
