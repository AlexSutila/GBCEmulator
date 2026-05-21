#include "savestate/codec.hpp"
#include "emu_types.hpp"
#include <cstdint>
#include <optional>
#include <stack>
#include <stdexcept>
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

  if (!incomplete.empty()) {
    auto &parent = incomplete.top();

    // Check to make sure we aren't about to overwrite the key if it exists
    if (std::holds_alternative<TreeRoot>(parent->val)) {
      auto &parent_ = std::get<TreeRoot>(parent->val);
      if (parent_.contains(tag)) [[unlikely]]
        throw std::runtime_error("Writer::start_complex_node() - key already found");
    }
  }

  // Should be good to insert
  auto t = std::make_shared<TreeNode>(TreeNode{
      .tag = tag,
      .val = TreeRoot({}),
  });
  incomplete.push(t);
}

void Writer::start_vector_node(TreeKey tag) {
  if (!build_tree)
    return;

  auto t = std::make_shared<TreeNode>(TreeNode{
      .tag = tag,
      .val = std::vector<std::shared_ptr<TreeNode>>({}),
  });
  incomplete.push(t);
}

void Writer::end_node() {
  if (!build_tree)
    return;

  auto completed = incomplete.top(); // Copy is intentional
  const TreeKey tag = completed->tag;
  incomplete.pop();

  if (incomplete.empty()) {
    root_node = std::move(*completed);
    return; // No parent, so skip
  }

  // Get the parent node to merge the sub-tree in
  auto &parent = incomplete.top();

  // Parent node is a complex type, so insert as a new field
  if (std::holds_alternative<TreeRoot>(parent->val)) {
    auto &parent_ = std::get<TreeRoot>(parent->val);
    if (parent_.contains(tag))
      throw std::runtime_error("Writer::end_node() - key already found");
    parent_[tag] = completed;
  }

  // Parent is a vector, so simply insert into the vector
  else if (std::holds_alternative<std::vector<std::shared_ptr<TreeNode>>>(parent->val)) {
    auto &parent_ = std::get<std::vector<std::shared_ptr<TreeNode>>>(parent->val);
    parent_.push_back(completed);
  }
}

void Writer::insert_into_top(TreeKey tag, TreeValue val) {
  if (incomplete.empty())
    throw std::runtime_error("Trying to insert into non-existent tree node");
  auto &top = incomplete.top();

  auto &elem = std::get<TreeRoot>(top->val);
  if (elem.contains(tag))
    throw std::runtime_error("Trying to insert into non-existent tree node");

  elem[tag] = std::make_shared<TreeNode>(TreeNode{
      .tag = tag,
      .val = val,
  });
}

TreeRoot Writer::get_tree() const {
  try {
    auto root = std::get<TreeNode>(root_node); // May not be populated bc of opt-in
    return std::get<TreeRoot>(root.val);
  }

  catch (std::bad_variant_access &ex) {
    return {}; // Just to avoid totally erroring out
  }
  __builtin_unreachable();
}

} // namespace Savestate
