#ifndef GBC_BOOT_HPP
#define GBC_BOOT_HPP

#include "emu_types.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace BootRomSizes {
/**
 * This ROM size allows for compatability with the following BIOS files:
 *  - DMG0
 *  - DMG
 *  - MGB
 *  - SGB (although we do not support additional SGB hardware)
 * These BIOS files are all contiguous in memory, starting from 0x0000
 */
constexpr std::size_t DMG = 0x100;
/**
 * This ROM size allows for compatability with the following BIOS files:
 *  - CGB0
 *  - CGB
 *  - AGB0
 *  - AGB
 * These BIOS files are not contiguous in memory, but instead are split in two
 * 0x100 and 0x800 byte chunks, leaving a 0x100 passthrough window back which
 * reads cartridge memory even when the BIOS is mapped.
 */
constexpr std::size_t CGB = 0x900;
} // namespace BootRomSizes

class BootROM {
public:
  explicit BootROM(const std::string &path);
  [[nodiscard]] byte_t read_byte(addr_t addr) const;
  [[nodiscard]] bool in_range(addr_t addr) const;

  /* In the scenario where a custom BIOS is provided, we use the rom size to
   * infer if we should start executing in CGB mode or not. Sooo cursed... */
  [[nodiscard]] bool is_large_rom() const;

private:
  std::vector<byte_t> rom_data{};
  std::size_t rom_size{};
};

#endif // GBC_BOOT_HPP
