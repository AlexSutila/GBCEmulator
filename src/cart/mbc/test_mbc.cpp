#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"
#include <vector>

// ---------------------------
// Test MBC
// ---------------------------
// This MBC does not exist lol. Basically, we want a convenient way to test the
// accuracy of the components we are emulating, so we devised this fabricated
// memory bank controller to act like a fat block of straight-up RAM. This MBC
// allows us to freely read and write whatever addresses we want.
//
// We have yet to support savestates on this mapper type because there is not
// much of a reason to implement it. If someone reading this wants to do it
// because they have some need for it, feel free.

class TestMbc final : public Mbc {
public:
  TestMbc() : Mbc(), ram_(0x10000, 0) {}

  // This doesn't need to be anything fancy, just use the full addr range
  void write(addr_t const addr, byte_t const val) override { ram_.at(addr) = val; }
  byte_t read(addr_t const addr) override { return ram_.at(addr); }

private:
  std::vector<byte_t> ram_{};
};

std::unique_ptr<Mbc> make_test_mbc() { return std::make_unique<TestMbc>(); }
