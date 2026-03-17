#include "memory/boot.hpp"
#include "format.hpp"

#include <cstddef>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>

BootROM::BootROM(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    throw std::runtime_error(IroGB::format("BootROM: Failed to open {}", path));

  /* BIOS must be one of two known sizes */
  rom_size = static_cast<std::size_t>(file.tellg());
  if (rom_size != BootRomSizes::DMG && rom_size != BootRomSizes::CGB)
    throw std::runtime_error("BootROM: Format is invalid");
  file.seekg(0, std::ios::beg);

  rom_data.resize(rom_size);
  if (!file.read(reinterpret_cast<char *>(rom_data.data()), static_cast<long long>(rom_size)))
    throw std::runtime_error("BootROM: Failed to fill buffer");
}

byte_t BootROM::read_byte(const addr_t addr) const {
  // Call `BootROM::in_range()` first to perform proper bounds checking.
  return rom_data.at(addr);
}

bool BootROM::in_range(const addr_t addr) const {
  switch (rom_size) {
  case BootRomSizes::DMG:
    return (addr < 0x0100);
  case BootRomSizes::CGB:
    // Leave a 0x100 gap to read the <redacted> logo from the cartridge
    return (addr < 0x0100) || (addr >= 0x200 && addr < 0x900);
  default:
    throw std::runtime_error("BootROM::in_range(): Corrupted size");
  }
}

bool BootROM::is_large_rom() const { return rom_size == BootRomSizes::CGB; }
