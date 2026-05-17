#include "savestate/codec.hpp"
#include "emu_types.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <stack>
#include <stdexcept>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Savestate {

struct Writer::Implementation {
public:
  using TreeKey = std::uint16_t;

  Implementation(bool should_build_tree) : build_tree(should_build_tree) {
    if (build_tree)
      start_complex_node(C_GBC);
  }

  // Always present in internal emulator state and consequentially the tree structure.
  void append_value(TreeKey tag, std::uint64_t val) {
    if (build_tree)
      insert_into_top(tag, val);
  }

  void append_value(TreeKey tag, const std::vector<byte_t> &val) {
    if (build_tree)
      insert_into_top(tag, val);
  }

  // Slightly more complicated since we just ignore this within the tree if it is not
  // actually populated within the internal state of the emulator itself.
  void append_value(TreeKey tag, const std::optional<std::uint64_t> &val) {
    if (build_tree && val.has_value())
      insert_into_top(tag, val.value());
  }

  void start_complex_node(TreeKey tag) {
    if (!build_tree)
      return;

    TreeNode t = {
        .tag = tag,
        .val = std::unordered_map<TreeKey, TreeNode>(),
    };
    incomplete.push(t);
  }

  void start_vector_node(TreeKey tag) {
    if (!build_tree)
      return;

    TreeNode t = {
        .tag = tag,
        .val = std::vector<TreeNode>(),
    };
    incomplete.push(t);
  }

  void end_node() {
    if (!build_tree)
      return;

    TreeNode completed = incomplete.top(); // Copy is intentional
    const TreeKey tag = completed.tag;
    incomplete.pop();

    // Get the parent node to merge the sub-tree in
    TreeNode &parent = incomplete.top();

    // Parent node is a complex type, so insert as a new field
    if (std::holds_alternative<std::unordered_map<TreeKey, TreeNode>>(parent.val)) {
      auto &parent_ = std::get<std::unordered_map<TreeKey, TreeNode>>(parent.val);
      parent_[tag] = completed;
    }

    // Parent is a vector, so simply insert into the vector
    else if (std::holds_alternative<std::vector<TreeNode>>(parent.val)) {
      auto &parent_ = std::get<std::vector<TreeNode>>(parent.val);
      parent_.push_back(completed);
    }
  }

private:
  struct TreeNode;

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
      std::vector<byte_t>,

      /**
       * Wraps vectors used for complex types requiring recursive descent:
       *  - `Writer::field_vector`
       */
      std::vector<TreeNode>,

      /**
       * Wraps raw complex types requiring recursive descent:
       *  - `Writer::field_complex`
       *  - `Writer::field_optional` (if value is present)
       */
      std::unordered_map<TreeKey, TreeNode>>;

  struct TreeNode {
    TreeKey tag{};
    TreeValue val{};
  };

  void insert_into_top(TreeKey tag, TreeValue val) {
    if (incomplete.empty())
      throw std::runtime_error("Trying to insert into non-existent tree node");
    TreeNode &top = incomplete.top();

    auto &elem = std::get<std::unordered_map<TreeKey, TreeNode>>(top.val);
    elem[tag] = {
        .tag = tag,
        .val = val,
    };
  }

  // Enables construction of the tree, if `false` no tree will be constructed to save on compute
  std::stack<TreeNode> incomplete{};
  const bool build_tree;
};

Writer::Writer(bool should_build_tree)
    : impl_(std::make_unique<Writer::Implementation>(should_build_tree)) {}
Writer::Writer() : impl_(std::make_unique<Writer::Implementation>(false)) {}
Writer::~Writer() = default;

void Writer::append_value(std::uint16_t tag, std::uint64_t val) {
  if (impl_) [[likely]]
    impl_->append_value(tag, val);
}

void Writer::append_value(std::uint16_t tag, const std::span<std::uint8_t> val) {
  if (impl_) [[likely]] {
    auto as_vec = std::vector<byte_t>(val.begin(), val.end());
    impl_->append_value(tag, as_vec);
  }
}

void Writer::append_value(std::uint16_t tag, const std::vector<std::uint8_t> &val) {
  if (impl_) [[likely]]
    impl_->append_value(tag, val);
}

void Writer::append_value(std::uint16_t tag, const std::optional<std::uint64_t> &val) {
  if (impl_) [[likely]]
    impl_->append_value(tag, val);
}

void Writer::start_complex_node(std::uint16_t tag) {
  if (impl_) [[likely]]
    impl_->start_complex_node(tag);
}

void Writer::start_vector_node(std::uint16_t tag) {
  if (impl_) [[likely]]
    impl_->start_vector_node(tag);
}

void Writer::end_node() {
  if (impl_) [[likely]]
    impl_->end_node();
}

} // namespace Savestate
