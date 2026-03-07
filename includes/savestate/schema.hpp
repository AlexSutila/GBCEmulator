#ifndef GBC_SCHEMA_HPP
#define GBC_SCHEMA_HPP

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

enum FieldTypes {
  SS_U8,
  SS_U16,
  SS_U32,
  SS_U64,

  /* Recurses into sub-structure */
  SS_CHUNK,
};

struct ss_field {
  FieldTypes type{};

  /**
   * Per chunk field identifier, allowing flexibility with field ordering. Tags
   * do not need to be listed in any particular order within a savestate file.
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

struct ss_chunk {
  std::uint16_t version{}; // Compatability indicator

  /**
   * Identifies a chunk (correlating to hardware component) by giving it a
   * recognizable name. Indicates which object to invoke the parser method on.
   */
  std::array<char, 4> tag{};

  /**
   * A list of fields that are to be set during the parse method of the
   * corresponding component.
   */
  std::vector<ss_field> fields{};
};

// Serialize
class Writer {
public:
  void u8(const std::uint8_t val) { buf_.push_back(val); }
  void u16(const std::uint16_t val) {
    u8(static_cast<std::uint8_t>(val & 0xFF));
    u8(static_cast<std::uint8_t>((val >> 8) & 0xFF));
  }

private:
  std::vector<std::uint8_t> buf_{};
};

// Deserialize
class Reader {
public:
  void u8(std::uint8_t &val) {
    require(1);
    val = buf_[pos_++];
  }
  void u16(std::uint16_t &val) {
    require(2);
    const auto b0 = static_cast<std::uint16_t>(buf_[pos_++]);
    const auto b1 = static_cast<std::uint16_t>(buf_[pos_++]);
    val = static_cast<std::uint16_t>(b0 | (b1 << 8));
  }

private:
  void require(const std::size_t n) const {
    if (pos_ + n > buf_.size())
      throw std::runtime_error("Savestate: truncated data");
  }

  std::span<const std::uint8_t> buf_;
  std::size_t pos_{0};
};

// For memory buffer size determinism
class Sizer {
public:
  void u8(std::uint8_t) { max_size_ += 1; }
  void u16(std::uint16_t) { max_size_ += 2; }

private:
  std::size_t max_size_{0};
};

#endif // GBC_SCHEMA_HPP
