#ifndef GBC_SCHEMA_HPP
#define GBC_SCHEMA_HPP

#include <cstdint>
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

struct field {
  FieldTypes type{};

  /**
   * Per chunk field identifier, allowing flexibility with field ordering.
   * Tags do not need to be listed in any particular order within a savestate
   * file.
   */
  std::uint16_t tag{};

  /**
   * Denotes how many `instances` of this field can occur. This should always
   * reflect the maximum value possible to make buffer allocation to load save
   * states into memory deterministic and not variable with internal state.
   */
  std::size_t count{};

  /**
   * For stateful `std::optional` types, which may or may not have a value. If
   * any field was not seen within the chunk, it should be reset.
   */
  bool optional{};
};

struct chunk {
  std::uint16_t version{}; // Compatability indicator

  /**
   * Identifies a chunk (correlating to hardware component) by giving it a
   * recognizable name. Indicates which object to invoke the parser method on.
   */
  std::uint16_t tag{};

  /**
   * A list of fields that are to be set during the parse method of the
   * corresponding component.
   */
  std::vector<field> fields{};
};

// Serialize
class Writer {
public:
  constexpr SavestateOps op() const { return OP_WRITE; }

  void u8(const std::uint8_t val) { buf_.push_back(val); }
  void boolean(const bool val) { u8(val ? 1 : 0); }

  void u16(const std::uint16_t val) {
    u8(static_cast<std::uint8_t>(val & 0xFF));
    u8(static_cast<std::uint8_t>((val >> 8) & 0xFF));
  }

  void field_u8(const std::uint16_t tag, const std::uint8_t val) {
    u16(tag);
    u8(val);
  }

  void field_boolean(const std::uint16_t tag, const bool val) {
    u16(tag);
    boolean(val);
  }

  void field_u16(const std::uint16_t tag, const std::uint16_t val) {
    u16(tag);
    u16(val);
  }

  void chunk(const std::uint16_t version, const std::uint16_t tag) {
    u16(version);
    u16(tag);
  }

  std::vector<std::uint8_t> get() const { return buf_; }

private:
  std::vector<std::uint8_t> buf_{};
};

// Deserialize
class Reader {
public:
  constexpr SavestateOps op() const { return OP_READ; }

  void u8(std::uint8_t &val) {
    require(1);
    val = buf_[pos_++];
  }
  void boolean(bool &val) {
    std::uint8_t read_val{};
    u8(read_val);
    val = static_cast<bool>(read_val);
  }
  void u16(std::uint16_t &val) {
    require(2);
    const auto b0 = static_cast<std::uint16_t>(buf_[pos_++]);
    const auto b1 = static_cast<std::uint16_t>(buf_[pos_++]);
    val = static_cast<std::uint16_t>(b0 | (b1 << 8));
  }

  void field_u8(const std::uint16_t tag, std::uint8_t &val) {
    check_tag(tag);
    u8(val);
  }

  void field_boolean(const std::uint16_t tag, bool &val) {
    check_tag(tag);
    boolean(val);
  }

  void field_u16(const std::uint16_t tag, std::uint16_t &val) {
    check_tag(tag);
    u16(val);
  }

  void chunk(const std::uint16_t version, const std::uint16_t tag) {
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
    std::uint16_t read_tag{};
    u16(read_tag);

    if (tag != read_tag)
      throw std::runtime_error("Savestate: chunk tag");
  }

  std::span<const std::uint8_t> buf_;
  std::size_t pos_{0};
};

// For memory buffer size determinism
class Sizer {
public:
  constexpr SavestateOps op() const { return OP_SIZE; }

  void u8(std::uint8_t) { max_size_ += 1; }
  void boolean(bool) { max_size_ += 1; }
  void u16(std::uint16_t) { max_size_ += 2; }

  void field_u8(const std::uint16_t tag, const std::uint8_t val) {
    u16(tag);
    u8(val);
  }

  void field_boolean(const std::uint16_t tag, const bool val) {
    u16(tag);
    boolean(val);
  }

  void field_u16(const std::uint16_t tag, const std::uint16_t val) {
    u16(tag);
    u16(val);
  }

  void chunk(const std::uint16_t version, const std::uint16_t tag) {
    u16(version);
    u16(tag);
  }

  const std::size_t get() const { return max_size_; }

private:
  std::size_t max_size_{0};
};

} // namespace Savestate

#endif // GBC_SCHEMA_HPP
