#ifndef GBC_FIFO_HPP
#define GBC_FIFO_HPP

#include "ppu/pixel.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

/*
 * Custom FIFO implemented via circular buffer to prevent repeated heap
 * allocations during rendering.
 */
template <typename T, std::size_t cap> class CircularFifo {
public:
  static_assert(cap > 0);
  static_assert((cap & (cap - 1)) == 0, "cap must be power of two");
  static constexpr std::size_t mask = cap - 1;

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return cap; }
  [[nodiscard]] std::size_t size() const noexcept { return count; }
  [[nodiscard]] bool full() const noexcept { return count == cap; }
  [[nodiscard]] bool empty() const noexcept { return count == 0; }

  // Pass an additional function template argument to allow for flexible typing
  template <typename T_, typename Fn> void parse_savestate(T_ &t, Fn &&fn) {
    t.field_generic(F_HEAD, head);
    t.field_generic(F_TAIL, tail);
    t.field_generic(F_COUNT, count);

    // Important this remains data-type agnostic
    t.field_complex(F_BUF, [&](auto &t) {
      for (std::size_t i{0}; i < cap; ++i)
        fn(t, buf[i]);
    });
  }

  void push(const T &value) {
    buf[head] = value;
    advance_head();
  }

  void push(T &&value) {
    buf[head] = std::move(value);
    advance_head();
  }

  T pop() {
    if (empty()) [[unlikely]]
      throw std::runtime_error("CircularFifo::pop() called on empty");

    T value = std::move(buf[tail]);
    tail = (tail + 1) % cap;
    --count;
    return value;
  }

  T &front() {
    if (empty()) [[unlikely]]
      throw std::runtime_error("CircularFifo::front() called on empty");
    return buf[tail];
  }

  [[nodiscard]] const T &front() const {
    if (empty()) [[unlikely]]
      throw std::runtime_error("CircularFifo::front() called on empty");
    return buf[tail];
  }

  T &at(const std::size_t index) {
    if (index >= count) [[unlikely]]
      throw std::out_of_range("CircularFifo::at() index out of range");
    return buf[(tail + index) % cap];
  }

  [[nodiscard]] const T &at(const std::size_t index) const {
    if (index >= count) [[unlikely]]
      throw std::out_of_range("CircularFifo::at() index out of range");
    return buf[(tail + index) % cap];
  }

  void clear() noexcept { head = tail = count = 0; }

private:
  void advance_head() {
    head = (head + 1) & mask;

    if (count < cap) {
      ++count;
    } else {
      tail = (tail + 1) & mask;
    }
  }

  enum : std::uint16_t {
    F_HEAD = 1,
    F_TAIL,
    F_COUNT,
    F_BUF,
  };

  std::size_t head{}, tail{}, count{};
  std::array<T, cap> buf{};
};

class BgPixelFifo {
public:
  template <typename T> void parse_savestate(T &t);
  BgPixelFifo();
  void flush();

  /* Pixels can only be pushed eight at a time, and popped if there would be at
   * least eight pixels remaining, so the rules here are kinda iffy. */
  [[nodiscard]] bool can_push() const;
  [[nodiscard]] bool can_pop() const;

  /* Should have error checking for over pushing/popping */
  void push(pixel px);
  pixel pop();

private:
  CircularFifo<pixel, 16> fifo;
};

class ObjPixelFifo {
public:
  template <typename T> void parse_savestate(T &t);
  ObjPixelFifo();
  void flush();

  /* The object fifo isn't... really... a fifo lol, because pixel data overlays
   * over other pixel data when sprites overlap. We need to poke holes when we
   * run into such circumstances. */
  [[nodiscard]] const pixel &at(std::size_t index) const;
  pixel &at(std::size_t index);
  void fill_transparent();

  /* Should have error checking for over pushing/popping */
  [[nodiscard]] bool can_pop() const;
  pixel pop();

private:
  CircularFifo<pixel, 8> fifo; // Yeah, pandocs is wrong lol
};

#endif // GBC_FIFO_HPP
