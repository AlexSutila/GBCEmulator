#include "ppu/fifo.hpp"
#include <cassert>
#include <cstddef>

constexpr pixel invisible = {
    .color_idx = 0, // Must be zero for transparent
    .palette_idx = 0,
    .discard = false,
};

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
ObjPixelFifo::ObjPixelFifo() : fifo(CircularFifo<pixel, 8>()) {
  fill_transparent();
}
void ObjPixelFifo::flush() {
  fifo.clear();
  fill_transparent();
}
void ObjPixelFifo::fill_transparent() {
  while (fifo.size() < fifo.capacity())
    fifo.push(invisible);
  assert(fifo.size() == fifo.capacity());
}

/* This FIFO is kinda bizzare, in that data pushed into it is sorta `overlayed`
 * rather than pushed into a circular FIFO. If a pixel has already been written,
 * the value will sustain but if it is transparent the new pixel is emplaced.
 * -----------------------------------------------------------------------------
 * Push omitted intentionally. Use `ObjPixelFifo::at()` instead. */
pixel ObjPixelFifo::pop() {
  auto ret = fifo.pop();
  fifo.push(invisible);

  /* Again, we have to maintain the size to match it's capacity. */
  assert(fifo.size() == fifo.capacity());
  return ret;
}

/* Lol #notafifo, poke the data in instead in transpatent locations */
const pixel &ObjPixelFifo::at(std::size_t index) const {
  return fifo.at(index);
}
pixel &ObjPixelFifo::at(std::size_t index) { return fifo.at(index); }
