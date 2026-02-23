#ifndef GBC_FIFO_HPP
#define GBC_FIFO_HPP

#include "ppu/pixel.hpp"
#include "savestate/codec.hpp"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace Savestate {
class Reader;
class Writer;
}

/*
 * Custom FIFO implemented via circular buffer to prevent repeated heap
 * allocations during rendering.
 */
template <typename T, std::size_t cap> class CircularFifo {
public:
  static_assert(cap > 0);

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return cap; }
  [[nodiscard]] std::size_t size() const noexcept { return count; }
  [[nodiscard]] bool full() const noexcept { return count == cap; }
  [[nodiscard]] bool empty() const noexcept { return count == 0; }

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

  [[nodiscard]] const T &front() const {
    if (empty())
      throw std::runtime_error("CircularFifo::front() called on empty");
    return buf[tail];
  }

  T &at(const std::size_t index) {
    if (index >= count)
      throw std::out_of_range("CircularFifo::at() index out of range");
    return buf[(tail + index) % cap];
  }

  [[nodiscard]] const T &at(const std::size_t index) const {
    if (index >= count)
      throw std::out_of_range("CircularFifo::at() index out of range");
    return buf[(tail + index) % cap];
  }

  void clear() noexcept { head = tail = count = 0; }
  void savestate_serialize(Savestate::Writer &out) const;
  void savestate_deserialize(Savestate::Reader &in);

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

template <typename T, std::size_t cap>
void CircularFifo<T, cap>::savestate_serialize(Savestate::Writer &out) const {
  static_assert(std::is_trivially_copyable_v<T>);
  out.field_u32(1, static_cast<std::uint32_t>(head));
  out.field_u32(2, static_cast<std::uint32_t>(tail));
  out.field_u32(3, static_cast<std::uint32_t>(count));
  out.field(4, [&](Savestate::Writer &w) {
    for (const auto &item : buf) {
      const auto *p = reinterpret_cast<const byte_t *>(&item);
      w.bytes({p, sizeof(T)});
    }
  });
}

template <typename T, std::size_t cap>
void CircularFifo<T, cap>::savestate_deserialize(Savestate::Reader &in) {
  static_assert(std::is_trivially_copyable_v<T>);
  while (const auto field = in.next_field()) {
    auto [id, payload] = *field;
    switch (id) {
    case 1:
      head = payload.u32();
      break;
    case 2:
      tail = payload.u32();
      break;
    case 3:
      count = payload.u32();
      break;
    case 4:
      for (auto &item : buf) {
        auto *p = reinterpret_cast<byte_t *>(&item);
        payload.bytes({p, sizeof(T)});
      }
      break;
    default:
      payload.skip(payload.remaining());
      break;
    }
    payload.expect_eof();
  }
  if (head >= cap || tail >= cap || count > cap)
    throw std::runtime_error("CircularFifo::savestate_deserialize()");
}

class BgPixelFifo {
public:
  BgPixelFifo();
  void flush();

  /* Pixels can only be pushed eight at a time, and popped if there would be at
   * least eight pixels remaining, so the rules here are kinda iffy. */
  [[nodiscard]] bool can_push() const;
  [[nodiscard]] bool can_pop() const;

  /* Should have error checking for over pushing/popping */
  void push(pixel px);
  pixel pop();
  void savestate_serialize(Savestate::Writer &out) const;
  void savestate_deserialize(Savestate::Reader &in);

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
  [[nodiscard]] const pixel &at(std::size_t index) const;
  pixel &at(std::size_t index);
  void fill_transparent();

  /* Should have error checking for over pushing/popping */
  [[nodiscard]] bool can_pop() const;
  pixel pop();
  void savestate_serialize(Savestate::Writer &out) const;
  void savestate_deserialize(Savestate::Reader &in);

private:
  CircularFifo<pixel, 8> fifo; // Yeah, pandocs is wrong lol
};

#endif // GBC_FIFO_HPP
