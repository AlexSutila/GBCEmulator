#ifndef __FIFO_H
#define __FIFO_H

#include "ppu/pixel.hpp"
#include <array>
#include <cstddef>
#include <stdexcept>

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
    advance_head();
  }

  void push(T &&value) {
    buf[head] = std::move(value);
    advance_head();
  }

  T pop() {
    if (empty())
      throw std::runtime_error("CircularFifo::pop() called on empty");

    T value = std::move(buf[tail]);
    tail = (tail + 1) % cap;
    --count;
    return value;
  }

  T &front() {
    if (empty())
      throw std::runtime_error("CircularFifo::front() called on empty");
    return buf[tail];
  }

  const T &front() const {
    if (empty())
      throw std::runtime_error("CircularFifo::front() called on empty");
    return buf[tail];
  }

  T &at(std::size_t index) {
    if (index >= count)
      throw std::out_of_range("CircularFifo::at() index out of range");
    return buf[(tail + index) % cap];
  }

  const T &at(std::size_t index) const {
    if (index >= count)
      throw std::out_of_range("CircularFifo::at() index out of range");
    return buf[(tail + index) % cap];
  }

  void clear() noexcept { head = tail = count = 0; }

private:
  void advance_head() {
    head = (head + 1) % cap;

    if (count < cap) {
      ++count;
    } else {
      tail = (tail + 1) % cap;
    }
  }

  std::size_t head{}, tail{}, count{};
  std::array<T, cap> buf{};
};

class BgPixelFifo {
public:
  BgPixelFifo();
  void flush();

  /* Pixels can only be pushed eight at a time, and popped if there would be at
   * least eight pixels remaining, so the rules here are kinda iffy. */
  bool can_push() const;
  bool can_pop() const;

  /* Should have error checking for over pushing/popping */
  void push(pixel px);
  pixel pop();

private:
  CircularFifo<pixel, 16> fifo;
};

class ObjPixelFifo {
public:
  ObjPixelFifo();
  void flush();

  /* The object fifo isn't... really... a fifo lol, because pixel data overlays
   * over other pixel data when sprites overlap. We need to poke holes when we
   * run into such circumstances. */
  const pixel &at(std::size_t index) const;
  pixel &at(std::size_t index);
  void fill_transparent();

  /* Should have error checking for over pushing/popping */
  bool can_pop() const;
  pixel pop();

private:
  CircularFifo<pixel, 8> fifo; // Yeah, pandocs is wrong lol
};

#endif // __FIFO_H
