#include "ppu/fifo.hpp"
#include <cstddef>

/* We can only push and pop to this under specific conditions. The hardware
 * tries to keep this FIFO populated with eight pixels minimum all the time. */
BgPixelFifo::BgPixelFifo() : fifo(CircularFifo<pixel, 16>()) {}
void BgPixelFifo::flush() { fifo.clear(); }
bool BgPixelFifo::can_push() const {
  constexpr std::size_t pixels_per_row = 8;
  return fifo.size() <= fifo.capacity() - pixels_per_row;
}
bool BgPixelFifo::can_pop() const { return fifo.size() > 8; }
void BgPixelFifo::push(pixel px) { fifo.push(px); }
pixel BgPixelFifo::pop() { return fifo.pop(); }

/* Pandocs is wrong, object pixel fifo is only eight pixels wide */
ObjPixelFifo::ObjPixelFifo() : fifo(CircularFifo<pixel, 8>()) {}
void ObjPixelFifo::flush() { fifo.clear(); }
void ObjPixelFifo::push(pixel px) { fifo.push(px); }
pixel ObjPixelFifo::pop() { return fifo.pop(); }

/* Lol #notafifo */
const pixel &ObjPixelFifo::at(std::size_t index) const {
  return fifo.at(index);
}
pixel &ObjPixelFifo::at(std::size_t index) { return fifo.at(index); }
