#ifndef __FIFO_H
#define __FIFO_H

#include "emu_types.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>

struct pixel {
  byte_t palette_idx; // A value between 0 and 3
};
class PixelProcessor;

/*
 * Custom FIFO implemented via circular buffer to prevent repeated heap
 * allocations during rendering.
 */
template <typename T, std::size_t cap> class CircularFifo {
public:
  static_assert(cap > 0);
  constexpr std::size_t capacity() const noexcept { return cap; }
  std::size_t size() const noexcept { return count; }
  bool full() const noexcept { return count == cap; }
  bool empty() const noexcept { return count == 0; }

  void push(const T &value) {
    buf[head] = value;
    head = (head + 1) % cap;

    if (count < cap)
      ++count;
    else
      tail = (tail + 1) % cap;
  }
  void push(T &&value) {
    buf[head] = std::move(value);
    head = (head + 1) % cap;

    if (count < cap)
      ++count;
    else
      tail = (tail + 1) % cap;
  }
  T pop() {
    if (empty())
      throw std::runtime_error("CircularFifo::pop() called on empty");

    T value = std::move(buf[tail]);
    tail = (tail + 1) % cap;
    --count;
    return value;
  };

  const T &front() const {
    if (empty())
      throw std::runtime_error("CircularFifo::front() called on empty");
    return buf[tail];
  }
  T &front() {
    if (empty())
      throw std::runtime_error("CircularFifo::front() called on empty");
    return buf[tail];
  }
  void clear() noexcept { head = tail = count = 0; }

private:
  std::size_t head{}, tail{}, count{};
  std::array<T, cap> buf{};
};

class PixelFifo {
public:
  PixelFifo(PixelProcessor *ppu_ptr);
  void reset();
  void step();

  /* Pop a fully processed pixel */
  bool can_pop() const { return fifo.size() > 0; }
  pixel pop() { return fifo.pop(); }

private:
  enum PixelFifoState {
    STATE_GET_TILE, // Miseading name, computes index
    STATE_GET_TILE_DATA_LOW,
    STATE_GET_TILE_DATA_HIGH,
    STATE_PUSH,
  };
  CircularFifo<pixel, 16> fifo;

  void get_tile();
  void get_tile_data_lo();
  void get_tile_data_hi();
  void do_push();

  std::size_t calc_tile_idx() const;
  byte_t fetch_tile_data(bool high) const;

  struct {
    std::size_t tile_idx;
    // A row of tile consists of two consecutive bytes
    byte_t data_lo;
    byte_t data_hi;
    // In unit of tiles - between 0 and 31
    std::size_t x_coor;
    // Y coor is tracked in pixels, can leverage LY register
  } fetcher;

  // For state transition logic
  std::optional<std::size_t> total_clks;
  std::size_t cur_clks;
  PixelFifoState state;

  PixelProcessor *const ppu;
};

#endif // __FIFO_H
