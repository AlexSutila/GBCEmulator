#ifndef GBC_SCHEMA_HPP
#define GBC_SCHEMA_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace Savestate {

enum SavestateOps {
  OP_READ,
  OP_WRITE,
  OP_SIZE,
};

enum ChunkTags {
  C_CPU,
  C_TIMER,
};

enum FieldTypes {
  U8,
  U16,
  U32,
  U64,

  /* Recurses into sub-structure */
  CHUNK,
};

// Serialize
class Writer {
public:
  constexpr SavestateOps op() const { return OP_WRITE; }

  template <typename Fn> void field(const std::uint16_t tag, Fn &&fn) {
    u16(tag);
    fn(*this);
  }

  template <typename Fn>
  void opt_field(const std::uint16_t tag, bool present, Fn &&fn) {
    u16(tag);
    if (present) {
      boolean(true);
      fn(*this);
    } else
      boolean(false);
  }

  void field_u8(const std::uint16_t tag, const std::uint8_t val) {
    field(tag, [&](Writer &w) { w.u8(val); });
  }
  void field_u8(const std::uint16_t tag,
                const std::optional<std::uint8_t> val) {
    opt_field(tag, val.has_value(), [&](Writer &w) { w.u8(val.value()); });
  }

  void field_boolean(const std::uint16_t tag, const bool val) {
    field(tag, [&](Writer &w) { w.boolean(val); });
  }
  void field_boolean(const std::uint16_t tag, const std::optional<bool> val) {
    opt_field(tag, val.has_value(), [&](Writer &w) { w.boolean(val.value()); });
  }

  void field_u16(const std::uint16_t tag, const std::uint16_t val) {
    field(tag, [&](Writer &w) { w.u16(val); });
  }
  void field_u16(const std::uint16_t tag,
                 const std::optional<std::uint16_t> val) {
    opt_field(tag, val.has_value(), [&](Writer &w) { w.u16(val.value()); });
  }

  void chunk_header(const std::uint16_t version, const std::uint16_t tag) {
    u16(version);
    u16(tag);
  }

  std::vector<std::uint8_t> get() const { return buf_; }

private:
  void u8(const std::uint8_t val) { buf_.push_back(val); }
  void boolean(const bool val) { u8(val ? 1 : 0); }

  void u16(const std::uint16_t val) {
    u8(static_cast<std::uint8_t>(val & 0xFF));
    u8(static_cast<std::uint8_t>((val >> 8) & 0xFF));
  }

  std::vector<std::uint8_t> buf_{};
};

// Deserialize
class Reader {
public:
  constexpr SavestateOps op() const { return OP_READ; }

  template <typename Fn> void field(const std::uint16_t tag, Fn &&fn) {
    check_tag(tag);
    fn(*this);
  }
  template <typename Fn, typename Opt>
  void opt_field(const std::uint16_t tag, std::optional<Opt> &val, Fn &&fn) {
    check_tag(tag);
    if (boolean()) {
      fn(*this);
    } else
      val.reset();
  }

  void field_u8(const std::uint16_t tag, std::uint8_t &val) {
    field(tag, [&](Reader &r) { val = r.u8(); });
  }
  void field_u8(const std::uint16_t tag, std::optional<std::uint8_t> &val) {
    opt_field(tag, val, [&](Reader &r) { val = r.u8(); });
  }

  void field_boolean(const std::uint16_t tag, bool &val) {
    field(tag, [&](Reader &r) { val = r.boolean(); });
  }
  void field_bool(const std::uint16_t tag, std::optional<bool> &val) {
    opt_field(tag, val, [&](Reader &r) { val = r.boolean(); });
  }

  void field_u16(const std::uint16_t tag, std::uint16_t &val) {
    field(tag, [&](Reader &r) { val = r.u16(); });
  }
  void field_u16(const std::uint16_t tag, std::optional<std::uint16_t> &val) {
    opt_field(tag, val, [&](Reader &r) { val = r.u16(); });
  }

  void chunk_header(const std::uint16_t version, const std::uint16_t tag) {
    const auto read_version = static_cast<std::uint16_t>(buf_[pos_++]);
    const auto read_tag = static_cast<std::uint16_t>(buf_[pos_++]);
    if (version != read_version)
      throw std::runtime_error("Savestate: bad version");
    if (tag != read_tag)
      throw std::runtime_error("Savestate: bad chunk version");
  }

private:
  void require(const std::size_t n) const {
    if (pos_ + n > buf_.size())
      throw std::runtime_error("Savestate: truncated data");
  }

  void check_tag(const std::uint16_t tag) {
    std::uint16_t read_tag = u16();
    if (tag != read_tag)
      throw std::runtime_error("Savestate: chunk tag");
  }

  std::uint8_t u8() {
    require(1);
    return buf_[pos_++];
  }

  bool boolean() {
    std::uint8_t read_val = u8();
    return static_cast<bool>(read_val);
  }

  std::uint16_t u16() {
    require(2);
    const auto b0 = static_cast<std::uint16_t>(buf_[pos_++]);
    const auto b1 = static_cast<std::uint16_t>(buf_[pos_++]);
    return static_cast<std::uint16_t>(b0 | (b1 << 8));
  }

  std::span<const std::uint8_t> buf_;
  std::size_t pos_{0};
};

// For memory buffer size determinism
class Sizer {
public:
  constexpr SavestateOps op() const { return OP_SIZE; }

  template <typename Fn> void field(Fn &&fn) {
    u16();
    fn(*this);
  }
  template <typename Fn> void opt_field(Fn &&fn) {
    u16();
    boolean();
    fn(*this);
  }

  void field_u8(const std::uint16_t tag, const std::uint8_t val) {
    field([&](Sizer &sz) { sz.u8(); });
  }
  void field_u8(const std::uint16_t tag, std::optional<std::uint8_t> val) {
    opt_field([&](Sizer &sz) { sz.u8(); });
  }

  void field_boolean(const std::uint16_t tag, const bool val) {
    field([&](Sizer &sz) { sz.boolean(); });
  }
  void field_boolean(const std::uint16_t tag, std::optional<bool> val) {
    opt_field([&](Sizer &sz) { sz.boolean(); });
  }

  void field_u16(const std::uint16_t tag, const std::uint16_t val) {
    field([&](Sizer &sz) { sz.u16(); });
  }
  void field_u16(const std::uint16_t tag, std::optional<std::uint16_t> val) {
    opt_field([&](Sizer &sz) { sz.u16(); });
  }

  void chunk_header(const std::uint16_t version, const std::uint16_t tag) {
    u16();
    u16();
  }

  const std::size_t get() const { return max_size_; }

private:
  void u8() { max_size_ += 1; }
  void boolean() { max_size_ += 1; }
  void u16() { max_size_ += 2; }

  std::size_t max_size_{0};
};

} // namespace Savestate

#endif // GBC_SCHEMA_HPP
