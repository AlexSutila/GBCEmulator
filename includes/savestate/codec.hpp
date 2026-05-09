#ifndef GBC_SCHEMA_HPP
#define GBC_SCHEMA_HPP

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace Savestate {

enum SavestateOps {
  OP_READ,
  OP_WRITE,
  OP_CHECK,
  OP_SIZE,
};

enum ChunkTags : std::uint16_t {
  C_GBC = 1,
  C_APU,
  C_CPU,
  C_SERIAL,
  C_TIMER,
  C_BUS,

  C_CART,
  C_MBC_EMS,
  C_MBC_SACHEN,
  C_MBC_HUC1,
  C_MBC_HUC3,
  C_MBC_M161,
  C_MBC_1,
  C_MBC_2,
  C_MBC_3,
  C_MBC_5,
  C_MBC_6,
  C_MBC_7,
  C_MBC_MMM01,
  C_MBC_TAMA5,
  // C_MBC_TEST, - We don't need this, but might want in future
  C_WISDOM_TREE, // ✝ Praise the Lord ✝

  C_PPU,
  C_FETCHER,
  C_OAM_DMA,
  C_VDMA,
  C_CRAM,

  /* The child schedulers do not hold any stateful information that is not already
   * initialized deterministicall by their constructor, so we don't need to worry
   * about them from a save-state perspective. */
  C_SCHEDULER,

  /* Denotes end of chunk */
  C_EOF = 0xFFFF
};

// Serialize
class Writer {
public:
  constexpr SavestateOps op() const { return OP_WRITE; }

  template <typename T> void field_generic(const std::uint16_t tag, const T val) {
    write<std::uint16_t>(tag);
    write<T>(val);
  }

  template <typename T> void field_enum(const std::uint16_t tag, const T val) {
    write<std::uint16_t>(tag);

    // This makes an assumption we don't need >256 enum values lol
    const std::uint8_t as_byte = static_cast<std::uint8_t>(val);
    write<std::uint8_t>(as_byte);
  }

  template <typename Fn> void field_complex(const std::uint16_t tag, Fn &&fn) {
    write<std::uint16_t>(tag);
    fn(*this);
    eof();
  }

  template <typename T, typename Fn>
  void field_vector(const std::uint16_t tag, std::vector<T> &vec, const std::size_t max_size,
                    Fn &&fn) {
    write<std::uint16_t>(tag);
    write<std::size_t>(vec.size());

    for (T &e : vec)
      fn(*this, e); // Should not manipulate, only write out

    eof();
  }

  void field_bytes(const std::uint16_t tag, std::span<std::uint8_t> bytes) {
    write<std::uint16_t>(tag);
    buf_.insert(buf_.end(), bytes.begin(), bytes.end());
  }

  template <typename T> void field_optional(const std::uint16_t tag, const std::optional<T> val) {
    write<std::uint16_t>(tag);
    if (val.has_value()) {
      write<bool>(true);
      write<T>(val.value());
    } else
      write<bool>(false);
  }

  template <typename T, typename Fn>
  void field_optional(const std::uint16_t tag, std::optional<T> &val, Fn &&fn) {
    write<std::uint16_t>(tag);

    const bool present = val.has_value();
    if (present) {
      fn(*this, *val);
      eof();
    }
  }

  void eof() { write<std::uint16_t>(C_EOF); }

  void chunk_header(const std::uint16_t version, const std::uint16_t tag) {
    write<std::uint16_t>(version);
    write<std::uint16_t>(tag);
  }

  std::vector<std::uint8_t> get() const { return buf_; }

private:
  template <typename T> void write(const T val) {
    static_assert(std::is_trivially_constructible_v<T>, "T must be trivially copyable");
    const auto *ptr = reinterpret_cast<const std::uint8_t *>(&val);
    buf_.insert(buf_.end(), ptr, ptr + sizeof(T));
  }

  std::vector<std::uint8_t> buf_{};
};

// Deserialize
class Reader {
public:
  Reader() = default;
  explicit Reader(const std::span<const std::uint8_t> bytes) : buf_(bytes) {}
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

  template <typename Fn> void field_complex(const std::uint16_t tag, Fn &&fn) {
    check_tag(tag);
    fn(*this);
    eof();
  }

  template <typename T, typename Fn>
  void field_vector(const std::uint16_t tag, std::vector<T> &vec, const std::size_t max_size,
                    Fn &&fn) {
    check_tag(tag);

    const std::size_t size = read<std::size_t>();
    if (size > max_size)
      throw std::runtime_error("Savestate: Exceeded vector capacity");

    vec.clear();
    vec.resize(size);

    for (T &e : vec)
      fn(*this, e); // Should populate this structure

    eof();
  }

  void field_bytes(const std::uint16_t tag, std::span<std::uint8_t> bytes) {
    check_tag(tag);

    require(bytes.size());
    for (std::size_t i{0}; i < bytes.size(); ++i)
      bytes[i] = buf_[pos_ + i];
    pos_ += bytes.size();
  }

  template <typename T> void field_optional(const std::uint16_t tag, std::optional<T> &val) {
    check_tag(tag);
    if (read<bool>())
      val = read<T>();
    else
      val.reset();
  }

  template <typename T, typename Fn>
  void field_optional(const std::uint16_t tag, std::optional<T> &val, Fn &&fn) {
    check_tag(tag);

    if (read<bool>()) {
      val.emplace();
      fn(*this, *val);
      eof();
    } else
      val.reset();
  }

  void eof() { check_tag(C_EOF); }

  void chunk_header(const std::uint16_t version, const std::uint16_t tag) {
    const auto read_version = read<std::uint16_t>();
    const auto read_tag = read<std::uint16_t>();
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
      throw std::runtime_error("Savestate: bad chunk tag");
  }

  template <typename T> T read() {
    static_assert(std::is_trivially_constructible_v<T>, "T must be trivially copyable");
    require(sizeof(T));

    T val;
    std::memcpy(&val, &buf_[pos_], sizeof(T));
    pos_ += sizeof(T);

    return val;
  }

  std::span<const std::uint8_t> buf_;
  std::size_t pos_{0};
};

// For a single pass recursive check of all tags, versions, and EOF flags
// ... also caution, I was lazy and let a clanker write this lol - Dorce
class Checker {
public:
  Checker() = default;
  explicit Checker(std::span<const std::uint8_t> bytes) : buf_(bytes) {}
  constexpr SavestateOps op() const { return OP_CHECK; }

  template <typename T> void field_generic(const std::uint16_t tag, const T) {
    check_tag(tag);
    skip<T>();
  }

  template <typename T> void field_enum(const std::uint16_t tag, const T) {
    check_tag(tag);
    skip<std::uint8_t>();
  }

  template <typename Fn> void field_complex(const std::uint16_t tag, Fn &&fn) {
    check_tag(tag);
    fn(*this);
    eof();
  }

  template <typename T, typename Fn>
  void field_vector(const std::uint16_t tag, std::vector<T> &, const std::size_t max_size,
                    Fn &&fn) {
    check_tag(tag);

    const std::size_t size = read<std::size_t>();
    if (size > max_size)
      throw std::runtime_error("Savestate: exceeded vector capacity");

    T dummy{};
    for (std::size_t i = 0; i < size; ++i)
      fn(*this, dummy);

    eof();
  }

  void field_bytes(const std::uint16_t tag, std::span<std::uint8_t> bytes) {
    check_tag(tag);
    require(bytes.size());
    pos_ += bytes.size();
  }

  template <typename T> void field_optional(const std::uint16_t tag, const std::optional<T>) {
    check_tag(tag);

    bool present = read<bool>();
    if (present)
      skip<T>();
  }

  template <typename T, typename Fn>
  void field_optional(const std::uint16_t tag, std::optional<T> &val, Fn &&fn) {
    check_tag(tag);

    if (read<bool>()) {
      T dummy{};
      fn(*this, dummy);
      eof();
    }
  }

  void eof() { check_tag(C_EOF); }

  void chunk_header(const std::uint16_t version, const std::uint16_t tag) {
    const auto read_version = read<std::uint16_t>();
    const auto read_tag = read<std::uint16_t>();
    if (version != read_version)
      throw std::runtime_error("Savestate: bad version");
    if (tag != read_tag)
      throw std::runtime_error("Savestate: bad chunk tag");
  }

private:
  void require(std::size_t n) const {
    if (pos_ + n > buf_.size())
      throw std::runtime_error("Savestate: truncated data");
  }

  void check_tag(std::uint16_t tag) {
    auto read_tag = read<std::uint16_t>();
    if (read_tag != tag)
      throw std::runtime_error("Savestate: bad field tag");
  }

  template <typename T> void skip() {
    require(sizeof(T));
    pos_ += sizeof(T);
  }

  template <typename T> T read() {
    require(sizeof(T));

    T val{0};
    for (std::size_t i = 0; i < sizeof(T); ++i)
      val |= static_cast<T>(buf_[pos_++] << (i * 8));

    return val;
  }

  std::span<const std::uint8_t> buf_;
  std::size_t pos_{0};
};

// For memory buffer size determinism
class Sizer {
public:
  constexpr SavestateOps op() const { return OP_SIZE; }

  template <typename T> void field_generic(const std::uint16_t tag, const T val) {
    parse<std::uint16_t>();
    parse<T>();
  }

  template <typename T> void field_enum(const std::uint16_t tag, const T val) {
    parse<std::uint16_t>();
    parse<std::uint8_t>(); // Always assume 8 bit
  }

  template <typename Fn> void field_complex(const std::uint16_t tag, Fn &&fn) {
    parse<std::uint16_t>();
    fn(*this);
    eof();
  }

  template <typename T, typename Fn>
  void field_vector(const std::uint16_t tag, std::vector<T> &vec, const std::size_t max_size,
                    Fn &&fn) {
    const T dummy{}; // Need this to have some object to pass, otherwise unused
    parse<std::uint16_t>();
    parse<std::size_t>();

    for (std::size_t i{0}; i < max_size; ++i)
      fn(*this, dummy); // Manipulate if you want, doesn't matter
    eof();
  }

  void field_bytes(const std::uint16_t tag, std::span<std::uint8_t> bytes) {
    parse<std::uint16_t>();
    max_size_ += bytes.size();
  }

  template <typename T> void field_optional(const std::uint16_t tag, const std::optional<T> val) {
    parse<std::uint16_t>();
    parse<bool>();
    parse<T>();
  }

  template <typename T, typename Fn>
  void field_optional(const std::uint16_t tag, std::optional<T> &val, Fn &&fn) {
    parse<std::uint16_t>();
    parse<bool>();

    T dummy{};
    fn(*this, dummy);
    eof();
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
