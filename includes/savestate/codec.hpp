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

enum ChunkTags : std::uint16_t {
  C_CPU = 1,
  C_TIMER,
  C_OAM_DMA,
  C_VDMA,

  /* Denotes end of chunk */
  C_EOF = 0xFFFF
};

// Serialize
class Writer {
public:
  constexpr SavestateOps op() const { return OP_WRITE; }

  template <typename T>
  void field_generic(const std::uint16_t tag, const T val) {
    write<std::uint16_t>(tag);
    write<T>(val);
  }

  template <typename T> void field_enum(const std::uint16_t tag, const T val) {
    write<std::uint16_t>(tag);

    // This makes an assumption we don't need >256 enum values lol
    const std::uint8_t as_byte = static_cast<std::uint8_t>(val);
    write<std::uint8_t>(as_byte);
  }

  template <typename T>
  void field_optional(const std::uint16_t tag, const std::optional<T> val) {
    write<std::uint16_t>(tag);
    if (val.has_value()) {
      write<bool>(true);
      write<T>(val.value());
    } else
      write<bool>(false);
  }

  void eof() { write<std::uint16_t>(C_EOF); }

  void chunk_header(const std::uint16_t version, const std::uint16_t tag) {
    write<std::uint16_t>(version);
    write<std::uint16_t>(tag);
  }

  std::vector<std::uint8_t> get() const { return buf_; }

private:
  template <typename T> void write(const T val) {
    for (std::size_t i = 0; i < sizeof(T); ++i)
      buf_.push_back(static_cast<std::uint8_t>((val >> (i * 8)) & 0xFF));
  }

  std::vector<std::uint8_t> buf_{};
};

// Deserialize
class Reader {
public:
  constexpr SavestateOps op() const { return OP_READ; }

  template <typename T> void field_generic(const std::uint16_t tag, T &val) {
    check_tag(tag);
    val = read<T>();
  }

  template <typename T> void field_enum(const std::uint16_t tag, T &val) {
    check_tag(tag);
    const std::uint8_t as_byte = read<std::uint8_t>();
    val = static_cast<T>(as_byte);
  }

  template <typename T>
  void field_optional(const std::uint16_t tag, std::optional<T> &val) {
    check_tag(tag);
    if (read<bool>())
      val = read<T>();
    else
      val.reset();
  }

  void eof() { check_tag(C_EOF); }

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
    std::uint16_t read_tag = read<std::uint16_t>();
    if (tag != read_tag)
      throw std::runtime_error("Savestate: chunk tag");
  }

  template <typename T> T read() {
    require(sizeof(T));

    T val{0};
    for (std::size_t i{0}; i < sizeof(T); ++i)
      val |= static_cast<std::uint8_t>(buf_[pos_++] << (i * 8));
    return val;
  }

  std::span<const std::uint8_t> buf_;
  std::size_t pos_{0};
};

// For memory buffer size determinism
class Sizer {
public:
  constexpr SavestateOps op() const { return OP_SIZE; }

  template <typename T>
  void field_generic(const std::uint16_t tag, const T val) {
    parse<std::uint16_t>();
    parse<T>();
  }

  template <typename T>
  void field_enum(const std::uint16_t tag, const T val) {
    parse<std::uint16_t>();
    parse<std::uint8_t>(); // Always assume 8 bit
  }

  template <typename T>
  void field_optional(const std::uint16_t tag, const std::optional<T> val) {
    parse<std::uint16_t>();
    parse<bool>();
    parse<T>();
  }

  void eof() { parse<std::uint16_t>(); }

  void chunk_header(const std::uint16_t version, const std::uint16_t tag) {
    parse<std::uint16_t>();
    parse<std::uint16_t>();
  }

  const std::size_t get() const { return max_size_; }

private:
  template <typename T> void parse() { max_size_ += sizeof(T); }
  std::size_t max_size_{0};
};

} // namespace Savestate

#endif // GBC_SCHEMA_HPP
