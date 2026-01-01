#ifndef __FIFO_H
#define __FIFO_H

#include "ppu/pixel.hpp"
#include <array>
#include <cstddef>
#include <stdexcept>

class PixelProcessingUnit;

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
  void flush();
  void step();

  /* Pop a fully processed pixel */
  bool can_push() const;
  bool can_pop() const;
  void push(pixel px);
  pixel pop();

private:
  CircularFifo<pixel, 16> fifo;
};

#endif // __FIFO_H
