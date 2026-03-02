#ifndef GBC_CART_HPP
#define GBC_CART_HPP

#include "cart/mbc.hpp"
#include "emu_types.hpp"
#include "mbc_creator.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace Savestate {
class Reader;
class Writer;
} // namespace Savestate

constexpr std::size_t kHeaderStart = 0x0100;
constexpr std::size_t kHeaderEnd = 0x014F;
// first opcode after header is typically at 0x0150
constexpr std::size_t kMinRomSize = 0x0150;

enum SpecialMbc {
  NotSpecial_t,
  MBC1M_t,
  MBC30_t,
  MMM01_t,
  M161_t,
  WisdomTree_t,
  Bung_t,
  EMS_t,
};

struct rom_header {
  std::array<byte_t, 4> entry_point{}; // 0100-0103
  std::array<byte_t, 16>
      title_area{}; // 0134-0143 (optionally title / manufacturer / cgb_flag)
  std::array<byte_t, 2> new_licensee_code{}; // 0144-0145
  byte_t sgb_flag{};                         // 0146
  byte_t cartridge_type{};                   // 0147
  byte_t rom_size_code{};                    // 0148
  byte_t ram_size_code{};                    // 0149
  byte_t destination_code{};                 // 014A
  byte_t old_licensee_code{};                // 014B
  byte_t mask_rom_version{};                 // 014C
  byte_t header_checksum{};                  // 014D
  std::uint16_t global_checksum{};           // 014E-014F (big-endian)

  [[nodiscard]] byte_t cgb_flag() const noexcept {
    return title_area[15];
  } // 0x0143
  // Best-effort: extract a title string
  [[nodiscard]] std::string title() const;
  // Best-effort: manufacturer code if it looks like 4 ASCII chars in 013F-0142
  // on CGB carts
  [[nodiscard]] std::string manufacturer_code() const;
};

struct cart {
  fs::path file_path{};
  std::vector<byte_t> rom{};
  rom_header header{};

  std::size_t declared_rom_bytes{}; // from header 0148
  std::size_t declared_ram_bytes{}; // from header 0149

  bool header_checksum_ok{};
  bool global_checksum_ok{};
  byte_t computed_header_checksum{};
  std::uint16_t computed_global_checksum{};
  SpecialMbc special_mbc{};

  [[nodiscard]] std::span<const byte_t> rom_span() const noexcept {
    return rom;
  }
  [[nodiscard]] const byte_t *rom_data() const noexcept { return rom.data(); }
  [[nodiscard]] std::size_t rom_size() const noexcept { return rom.size(); }
};

class Cartridge {
public:
  explicit Cartridge(cart image)
      : image_(std::move(image)), mbc_(make_mbc(image_)) {}
  explicit Cartridge() : image_({}), mbc_(make_test_mbc()) {}

  [[nodiscard]] byte_t read_byte(const addr_t addr) const {
    return mbc_->read(addr);
  }
  void write(addr_t addr, byte_t v);

  [[nodiscard]] const cart &image() const noexcept { return image_; }

  [[nodiscard]] bool has_battery() const noexcept {
    return mbc_->has_battery();
  }
  [[nodiscard]] std::span<const byte_t> ram() const noexcept {
    return mbc_->ram();
  }
  [[nodiscard]] std::span<byte_t> ram() noexcept { return mbc_->ram(); }
  bool load_save_file(const fs::path &save_path);
  bool write_save_file(const fs::path &save_path) const;
  void savestate_serialize(Savestate::Writer &out) const;
  void savestate_deserialize(Savestate::Reader &in);
  bool consume_sram_save() noexcept;

private:
  cart image_;
  std::unique_ptr<Mbc> mbc_;
  bool save_dirty_{false};
};

[[nodiscard]] cart load_cart_raw(std::vector<byte_t> rom_bytes);
[[nodiscard]] cart load_cart_fs(const fs::path &rom_path);

// helpers
[[nodiscard]] std::size_t rom_bytes_from_code(byte_t code);
[[nodiscard]] std::size_t ram_bytes_from_code(byte_t code);
[[nodiscard]] bool cgb_enabled(byte_t cgb_flag);

#endif // GBC_CART_HPP
