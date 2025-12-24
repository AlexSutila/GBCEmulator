#ifndef __FIFO_H
#define __FIFO_H

#include "emu_types.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>

struct pixel {
  byte_t color; // A value between 0 and 3
};

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
  PixelFifo();
  void step();

private:
  enum PixelFifoState {
    STATE_GET_TILE,
    STATE_GET_TILE_DATA_LOW,
    STATE_GET_TILE_DATA_HIGH,
    STATE_SLEEP,
    // Tried every dot until it succeeds
    STATE_PUSH,
  };
  CircularFifo<pixel, 16> fifo;

  void get_tile();
  void get_tile_data();
  void sleep();

  // For state transition logic
  std::optional<std::size_t> cur_clks;
  std::size_t max_clks;
  PixelFifoState state;
};

#endif // __FIFO_H
