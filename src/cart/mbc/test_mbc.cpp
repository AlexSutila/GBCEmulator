#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

// ---------------------------
// Test MBC
// ---------------------------
// This MBC does not exist lol. Basically, we want a convenient way to test the
// accuracy of the components we are emulating, so we devised this fabricated
// memory bank controller to act like a fat block of straight-up RAM. This MBC
// allows us to freely read and write whatever addresses we want.

class TestMbc final : public Mbc {
public:
  TestMbc() : Mbc(), ram_(0x10000, 0) {}

  // This doesn't need to be anything fancy, just use the full addr range
  void write(addr_t const addr, byte_t const val) override {
    ram_.at(addr) = val;
  }
  byte_t read(addr_t const addr) override { return ram_.at(addr); }

private:
  std::vector<byte_t> ram_{};
};

std::unique_ptr<Mbc> make_test_mbc() {
  // TODO: Might be nice to have a way to force CGB/DMG modes
  return std::make_unique<TestMbc>();
}
