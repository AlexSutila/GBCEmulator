#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"
#include "savestate/codec.hpp"
#include <stdexcept>

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
  [[nodiscard]] const char *savestate_tag() const noexcept override {
    return "TEST";
  }
  void savestate_serialize(Savestate::Writer &out) const override {
    out.field_u32(1, static_cast<std::uint32_t>(ram_.size()));
    out.field(2, [&](Savestate::Writer &w) { w.bytes(ram_); });
  }
  void savestate_deserialize(Savestate::Reader &in) override {
    bool got_size = false;
    bool got_ram = false;
    while (const auto field = in.next_field()) {
      auto [id, payload] = *field;
      switch (id) {
      case 1:
        if (const auto size = static_cast<std::size_t>(payload.u32());
            size != ram_.size())
          throw std::runtime_error("TestMbc::savestate_deserialize()");
        got_size = true;
        break;
      case 2:
        if (payload.remaining() != ram_.size())
          throw std::runtime_error("TestMbc::savestate_deserialize()");
        payload.bytes(ram_);
        got_ram = true;
        break;
      default:
        payload.skip(payload.remaining());
        break;
      }
      payload.expect_eof();
    }
    if (!got_size || !got_ram)
      throw std::runtime_error("TestMbc::savestate_deserialize()");
  }

private:
  std::vector<byte_t> ram_{};
};

std::unique_ptr<Mbc> make_test_mbc() {
  // TODO: Might be nice to have a way to force CGB/DMG modes
  return std::make_unique<TestMbc>();
}
