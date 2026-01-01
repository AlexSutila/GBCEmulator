#include "ppu/fifo.hpp"
#include <cstddef>

PixelFifo::PixelFifo()
    : fifo(CircularFifo<pixel, 16>()) {}

void PixelFifo::flush() { fifo.clear(); }
bool PixelFifo::can_push() const {
  constexpr std::size_t pixels_per_row = 8;
  return fifo.size() <= fifo.capacity() - pixels_per_row;
}
bool PixelFifo::can_pop() const { return fifo.size() > 8; }
void PixelFifo::push(pixel px) { fifo.push(px); }
pixel PixelFifo::pop() { return fifo.pop(); }
