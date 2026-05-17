#include "savestate/codec.hpp"
#include "emu_types.hpp"
#include <cstdint>
#include <optional>
#include <stack>
#include <stdexcept>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Savestate {

void Writer::append_value(TreeKey tag, std::uint64_t val) {
  if (build_tree)
    insert_into_top(tag, val);
}

void Writer::append_value(TreeKey tag, const std::vector<byte_t> &val) {
  if (build_tree)
    insert_into_top(tag, val);
}

void Writer::append_value(TreeKey tag, const std::optional<std::uint64_t> &val) {
  if (build_tree && val.has_value())
    insert_into_top(tag, val.value());
}

void Writer::start_complex_node(TreeKey tag) {
  if (!build_tree)
    return;

  TreeNode t = {
      .tag = tag,
      .val = std::unordered_map<TreeKey, TreeNode>(),
  };
  incomplete.push(t);
}

void Writer::start_vector_node(TreeKey tag) {
  if (!build_tree)
    return;

  TreeNode t = {
      .tag = tag,
      .val = std::vector<TreeNode>(),
  };
  incomplete.push(t);
}

void Writer::end_node() {
  if (!build_tree)
    return;

  TreeNode completed = incomplete.top(); // Copy is intentional
  const TreeKey tag = completed.tag;
  incomplete.pop();

  if (incomplete.empty()) {
    root_node = std::move(completed);
    return; // No parent, so skip
  }

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

void Writer::insert_into_top(TreeKey tag, TreeValue val) {
  if (incomplete.empty())
    throw std::runtime_error("Trying to insert into non-existent tree node");
  TreeNode &top = incomplete.top();

  auto &elem = std::get<std::unordered_map<TreeKey, TreeNode>>(top.val);
  elem[tag] = {
      .tag = tag,
      .val = val,
  };
}

std::unordered_map<TreeKey, TreeNode> Writer::get_tree() const {
  try {
    auto root = std::get<TreeNode>(root_node); // May not be populated bc of opt-in
    return std::get<std::unordered_map<TreeKey, TreeNode>>(root.val);
  }

  catch (std::bad_variant_access &ex) {
    return {}; // Just to avoid totally erroring out
  }
  __builtin_unreachable();
}

} // namespace Savestate
