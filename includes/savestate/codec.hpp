#ifndef GBC_SAVESTATE_CODEC_HPP
#define GBC_SAVESTATE_CODEC_HPP

#include "emu_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Savestate {

struct Field;
struct Chunk;

class Writer {
public:
  void u8(const byte_t v) { buf_.push_back(v); }

  void boolean(const bool v) { u8(v ? 1 : 0); }

  void u16(const std::uint16_t v) {
    u8(static_cast<byte_t>(v & 0xFF));
    u8(static_cast<byte_t>((v >> 8) & 0xFF));
  }

  void u32(const std::uint32_t v) {
    u8(static_cast<byte_t>(v & 0xFF));
    u8(static_cast<byte_t>((v >> 8) & 0xFF));
    u8(static_cast<byte_t>((v >> 16) & 0xFF));
    u8(static_cast<byte_t>((v >> 24) & 0xFF));
  }

  void u64(const std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
      u8(static_cast<byte_t>((v >> (i * 8)) & 0xFF));
  }

  void bytes(std::span<const byte_t> bytes_) {
    buf_.insert(buf_.end(), bytes_.begin(), bytes_.end());
  }

  [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }

  template <typename Fn> void field(const std::uint16_t id, Fn &&fn) {
    u16(id);
    const auto size_pos = reserve_u32_();
    const auto payload_start = size();
    fn(*this);
    patch_u32_(size_pos, static_cast<std::uint32_t>(size() - payload_start));
  }

  void field_u8(const std::uint16_t id, const byte_t v) {
    field(id, [&](Writer &w) { w.u8(v); });
  }
  void field_bool(const std::uint16_t id, const bool v) {
    field(id, [&](Writer &w) { w.boolean(v); });
  }
  void field_u16(const std::uint16_t id, const std::uint16_t v) {
    field(id, [&](Writer &w) { w.u16(v); });
  }
  void field_u32(const std::uint16_t id, const std::uint32_t v) {
    field(id, [&](Writer &w) { w.u32(v); });
  }
  void field_u64(const std::uint16_t id, const std::uint64_t v) {
    field(id, [&](Writer &w) { w.u64(v); });
  }

  template <typename Fn>
  void chunk(const std::string_view tag4, const std::uint16_t version, Fn &&fn) {
    if (tag4.size() != 4)
      throw std::runtime_error("Savestate: chunk tag must be 4 bytes");
    tag(tag4);
    u16(version);
    const auto size_pos = reserve_u32_();
    const auto payload_start = size();
    fn(*this);
    patch_u32_(size_pos, static_cast<std::uint32_t>(size() - payload_start));
  }

  template <std::size_t N> void tag(const std::array<char, N> &tag_) {
    for (const char c : tag_)
      u8(static_cast<byte_t>(c));
  }

  void tag(const std::string_view tag_) {
    for (const char c : tag_)
      u8(static_cast<byte_t>(c));
  }

  [[nodiscard]] const std::vector<byte_t> &data() const noexcept { return buf_; }
  [[nodiscard]] std::vector<byte_t> take() && { return std::move(buf_); }

private:
  [[nodiscard]] std::size_t reserve_u32_() {
    const auto pos = buf_.size();
    buf_.resize(pos + 4, 0);
    return pos;
  }

  void patch_u32_(const std::size_t pos, const std::uint32_t v) {
    if (pos + 4 > buf_.size())
      throw std::runtime_error("Savestate: invalid patch offset");
    buf_[pos + 0] = static_cast<byte_t>(v & 0xFF);
    buf_[pos + 1] = static_cast<byte_t>((v >> 8) & 0xFF);
    buf_[pos + 2] = static_cast<byte_t>((v >> 16) & 0xFF);
    buf_[pos + 3] = static_cast<byte_t>((v >> 24) & 0xFF);
  }

  std::vector<byte_t> buf_{};
};

class Reader {
public:
  Reader() = default;
  explicit Reader(const std::span<const byte_t> bytes) : buf_(bytes) {}

  [[nodiscard]] bool empty() const noexcept { return pos_ >= buf_.size(); }
  [[nodiscard]] std::size_t remaining() const noexcept { return buf_.size() - pos_; }
  void skip(const std::size_t n) {
    require(n);
    pos_ += n;
  }

  [[nodiscard]] Reader subreader(const std::size_t n) {
    require(n);
    const auto start = pos_;
    pos_ += n;
    return Reader(buf_.subspan(start, n));
  }

  void expect_eof() const {
    if (!empty())
      throw std::runtime_error("Savestate: trailing field data");
  }

  byte_t u8() {
    require(1);
    return buf_[pos_++];
  }

  bool boolean() {
    const byte_t v = u8();
    if (v > 1)
      throw std::runtime_error("Savestate: invalid boolean");
    return v != 0;
  }

  std::uint16_t u16() {
    require(2);
    const auto b0 = static_cast<std::uint16_t>(buf_[pos_++]);
    const auto b1 = static_cast<std::uint16_t>(buf_[pos_++]);
    return static_cast<std::uint16_t>(b0 | (b1 << 8));
  }

  std::uint32_t u32() {
    require(4);
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
      v |= static_cast<std::uint32_t>(buf_[pos_++]) << (i * 8);
    return v;
  }

  std::uint64_t u64() {
    require(8);
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
      v |= static_cast<std::uint64_t>(buf_[pos_++]) << (i * 8);
    return v;
  }

  void bytes(std::span<byte_t> out) {
    require(out.size());
    for (std::size_t i = 0; i < out.size(); ++i)
      out[i] = buf_[pos_ + i];
    pos_ += out.size();
  }

  void expect_tag(const std::string_view tag_) {
    require(tag_.size());
    for (const char c : tag_) {
      if (const auto got = static_cast<char>(u8()); got != c)
        throw std::runtime_error("Savestate: invalid tag");
    }
  }

  [[nodiscard]] std::optional<Field> next_field();
  [[nodiscard]] std::optional<Chunk> next_chunk();

private:
  void require(const std::size_t n) const {
    if (pos_ + n > buf_.size())
      throw std::runtime_error("Savestate: truncated data");
  }

  std::span<const byte_t> buf_;
  std::size_t pos_{0};
};

struct Field {
  std::uint16_t id{};
  Reader payload{};
};

struct Chunk {
  std::array<char, 4> tag{};
  std::uint16_t version{};
  Reader payload{};
};

inline std::optional<Field> Reader::next_field() {
  if (empty())
    return std::nullopt;
  const auto id = u16();
  const auto len = static_cast<std::size_t>(u32());
  return Field{.id = id, .payload = subreader(len)};
}

inline std::optional<Chunk> Reader::next_chunk() {
  if (empty())
    return std::nullopt;
  std::array<char, 4> tag_{};
  for (auto &c : tag_)
    c = static_cast<char>(u8());
  const auto version = u16();
  const auto len = static_cast<std::size_t>(u32());
  return Chunk{.tag = tag_, .version = version, .payload = subreader(len)};
}

} // namespace Savestate

#endif // GBC_SAVESTATE_CODEC_HPP
