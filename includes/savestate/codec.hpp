#ifndef GBC_SCHEMA_HPP
#define GBC_SCHEMA_HPP

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stack>
#include <stdexcept>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Savestate {

struct TreeNode;
using TreeKey = std::uint16_t;
using TreeRoot = std::unordered_map<TreeKey, std::shared_ptr<TreeNode>>;

enum SavestateOps {
  OP_READ,
  OP_WRITE,
  OP_CHECK,
  OP_SIZE,
};

enum ChunkTags : TreeKey {
  C_GBC = 1,
  C_SYS,
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

  C_CHILD_SCHEDULER,
  C_SYS_SCHEDULER,

  /* Denotes end of chunk */
  C_EOF = 0xFFFF
};

// Convenience type to represent the state of any individual savestate field
using TreeValue = std::variant<

    /**
     * Wraps trivially convertible types parsed by:
     *  - `Writer::field_generic`
     *  - `Writer::field_enum`
     *  - `Writer::field_optional` (if value is present)
     */
    std::uint64_t,

    /**
     * Wraps vectors used for implementing standard memory heirarchy:
     *  - `Writer::field_bytes`
     */
    std::vector<std::uint8_t>,

    /**
     * Wraps vectors used for complex types requiring recursive descent:
     *  - `Writer::field_vector`
     */
    std::vector<std::shared_ptr<TreeNode>>,

    /**
     * Wraps raw complex types requiring recursive descent:
     *  - `Writer::field_complex`
     *  - `Writer::field_optional` (if value is present)
     *
     * Just because the type is `TreeRoot`, doesn't mean this is always the
     * main root of the tree. This also accounts for roots of subtrees
     */
    TreeRoot>;

struct TreeNode {
  TreeKey tag{};
  TreeValue val{};
};

// Serialize
class Writer {
public:
  Writer(bool should_build_tree) : root_node(std::monostate()), build_tree(should_build_tree) {}
  Writer() : root_node(std::monostate()), build_tree(false) {} // Tree construction is opt-in
  ~Writer() = default;

  constexpr SavestateOps op() const { return OP_WRITE; }

  template <typename T> void field_generic(const TreeKey tag, const T val) {
    write<TreeKey>(tag);
    write<T>(val);
    append_value(tag, static_cast<std::uint64_t>(val));
  }

  template <typename T> void field_enum(const TreeKey tag, const T val) {
    write<TreeKey>(tag);

    // This makes an assumption we don't need >256 enum values lol
    const std::uint8_t as_byte = static_cast<std::uint8_t>(val);
    write<std::uint8_t>(as_byte);
    append_value(tag, static_cast<std::uint64_t>(val));
  }

  template <typename Fn> void field_complex(const TreeKey tag, Fn &&fn) {
    write<TreeKey>(tag);
    start_complex_node(tag);

    fn(*this);
    eof();
  }

  template <typename T, typename Fn>
  void field_vector(const TreeKey tag, std::vector<T> &vec, const std::size_t max_size, Fn &&fn) {
    write<TreeKey>(tag);
    write<std::size_t>(vec.size());

    // We don't need to consider the size here, just ignore
    start_vector_node(tag);
    for (T &e : vec) {
      start_complex_node(tag); // Tag will be duplicated, its not a big deal
      fn(*this, e);            // Should not manipulate, only write out
      eof();
    }
    eof();
  }

  void field_bytes(const TreeKey tag, std::span<std::uint8_t> bytes) {
    write<TreeKey>(tag);
    buf_.insert(buf_.end(), bytes.begin(), bytes.end());

    const auto as_vec = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    append_value(tag, as_vec);
  }

  template <typename T> void field_optional(const TreeKey tag, const std::optional<T> val) {
    write<TreeKey>(tag);
    if (val.has_value()) {
      write<bool>(true);
      write<T>(val.value());
    } else
      write<bool>(false);
  }

  template <typename T, typename Fn>
  void field_optional(const TreeKey tag, std::optional<T> &val, Fn &&fn) {
    write<TreeKey>(tag);

    const bool present = val.has_value();
    if (present) {
      fn(*this, *val);
      eof();
    }
  }

  void eof() {
    write<TreeKey>(C_EOF);
    end_node();
  }

  void chunk_header(const std::uint16_t version, const TreeKey tag) {
    write<std::uint16_t>(version);
    write<TreeKey>(tag);

    // Chunk headers are written for complex sub-structures that essentially just behave like
    // complex nodes. The only difference is they have a version which the tree will ignore.
    start_complex_node(tag);
  }

  std::vector<std::uint8_t> get() const { return buf_; }
  TreeRoot get_tree() const;

private:
  template <typename T> void write(const T val) {
    static_assert(std::is_trivially_constructible_v<T>, "T must be trivially copyable");
    const auto *ptr = reinterpret_cast<const std::uint8_t *>(&val);
    buf_.insert(buf_.end(), ptr, ptr + sizeof(T));
  }

  void insert_into_top(TreeKey tag, TreeValue val);
  void append_value(TreeKey tag, std::uint64_t val);
  void append_value(TreeKey tag, const std::vector<std::uint8_t> &val);
  void append_value(TreeKey tag, const std::optional<std::uint64_t> &val);
  void start_complex_node(TreeKey tag);
  void start_vector_node(TreeKey tag);
  void end_node();

  // Byte stream buffer for serialized emulator state
  std::vector<std::uint8_t> buf_{};

  // Opt-in tree-like structure of emulator state
  std::variant<TreeNode, std::monostate> root_node{std::monostate()};
  std::stack<std::shared_ptr<TreeNode>> incomplete{};
  const bool build_tree;
};

// Deserialize
class Reader {
public:
  Reader() = default;
  explicit Reader(const std::span<const std::uint8_t> bytes) : buf_(bytes) {}
  constexpr SavestateOps op() const { return OP_READ; }

  template <typename T> void field_generic(const TreeKey tag, T &val) {
    check_tag(tag);
    val = read<T>();
  }

  template <typename T> void field_enum(const TreeKey tag, T &val) {
    check_tag(tag);
    const std::uint8_t as_byte = read<std::uint8_t>();
    val = static_cast<T>(as_byte);
  }

  template <typename Fn> void field_complex(const TreeKey tag, Fn &&fn) {
    check_tag(tag);
    fn(*this);
    eof();
  }

  template <typename T, typename Fn>
  void field_vector(const TreeKey tag, std::vector<T> &vec, const std::size_t max_size, Fn &&fn) {
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

  void field_bytes(const TreeKey tag, std::span<std::uint8_t> bytes) {
    check_tag(tag);

    require(bytes.size());
    for (std::size_t i{0}; i < bytes.size(); ++i)
      bytes[i] = buf_[pos_ + i];
    pos_ += bytes.size();
  }

  template <typename T> void field_optional(const TreeKey tag, std::optional<T> &val) {
    check_tag(tag);
    if (read<bool>())
      val = read<T>();
    else
      val.reset();
  }

  template <typename T, typename Fn>
  void field_optional(const TreeKey tag, std::optional<T> &val, Fn &&fn) {
    check_tag(tag);

    if (read<bool>()) {
      val.emplace();
      fn(*this, *val);
      eof();
    } else
      val.reset();
  }

  void eof() { check_tag(C_EOF); }

  void chunk_header(const std::uint16_t version, const TreeKey tag) {
    const auto read_version = read<std::uint16_t>();
    const auto read_tag = read<TreeKey>();
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

  void check_tag(const TreeKey tag) {
    TreeKey read_tag = read<TreeKey>();
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

  template <typename T> void field_generic(const TreeKey tag, const T) {
    check_tag(tag);
    skip<T>();
  }

  template <typename T> void field_enum(const TreeKey tag, const T) {
    check_tag(tag);
    skip<std::uint8_t>();
  }

  template <typename Fn> void field_complex(const TreeKey tag, Fn &&fn) {
    check_tag(tag);
    fn(*this);
    eof();
  }

  template <typename T, typename Fn>
  void field_vector(const TreeKey tag, std::vector<T> &, const std::size_t max_size, Fn &&fn) {
    check_tag(tag);

    const std::size_t size = read<std::size_t>();
    if (size > max_size)
      throw std::runtime_error("Savestate: exceeded vector capacity");

    T dummy{};
    for (std::size_t i = 0; i < size; ++i)
      fn(*this, dummy);

    eof();
  }

  void field_bytes(const TreeKey tag, std::span<std::uint8_t> bytes) {
    check_tag(tag);
    require(bytes.size());
    pos_ += bytes.size();
  }

  template <typename T> void field_optional(const TreeKey tag, const std::optional<T>) {
    check_tag(tag);

    bool present = read<bool>();
    if (present)
      skip<T>();
  }

  template <typename T, typename Fn>
  void field_optional(const TreeKey tag, std::optional<T> &val, Fn &&fn) {
    check_tag(tag);

    if (read<bool>()) {
      T dummy{};
      fn(*this, dummy);
      eof();
    }
  }

  void eof() { check_tag(C_EOF); }

  void chunk_header(const std::uint16_t version, const TreeKey tag) {
    const auto read_version = read<std::uint16_t>();
    const auto read_tag = read<TreeKey>();
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

  void check_tag(TreeKey tag) {
    auto read_tag = read<TreeKey>();
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

  template <typename T> void field_generic(const TreeKey tag, const T val) {
    parse<TreeKey>();
    parse<T>();
  }

  template <typename T> void field_enum(const TreeKey tag, const T val) {
    parse<TreeKey>();
    parse<std::uint8_t>(); // Always assume 8 bit
  }

  template <typename Fn> void field_complex(const TreeKey tag, Fn &&fn) {
    parse<TreeKey>();
    fn(*this);
    eof();
  }

  template <typename T, typename Fn>
  void field_vector(const TreeKey tag, std::vector<T> &vec, const std::size_t max_size, Fn &&fn) {
    const T dummy{}; // Need this to have some object to pass, otherwise unused
    parse<TreeKey>();
    parse<std::size_t>();

    for (std::size_t i{0}; i < max_size; ++i)
      fn(*this, dummy); // Manipulate if you want, doesn't matter
    eof();
  }

  void field_bytes(const TreeKey tag, std::span<std::uint8_t> bytes) {
    parse<TreeKey>();
    max_size_ += bytes.size();
  }

  template <typename T> void field_optional(const TreeKey tag, const std::optional<T> val) {
    parse<TreeKey>();
    parse<bool>();
    parse<T>();
  }

  template <typename T, typename Fn>
  void field_optional(const TreeKey tag, std::optional<T> &val, Fn &&fn) {
    parse<TreeKey>();
    parse<bool>();

    T dummy{};
    fn(*this, dummy);
    eof();
  }
  void eof() { parse<TreeKey>(); }

  void chunk_header(const std::uint16_t version, const TreeKey tag) {
    parse<std::uint16_t>();
    parse<TreeKey>();
  }

  const std::size_t get() const { return max_size_; }

private:
  template <typename T> void parse() { max_size_ += sizeof(T); }
  std::size_t max_size_{0};
};

} // namespace Savestate

#endif // GBC_SCHEMA_HPP
