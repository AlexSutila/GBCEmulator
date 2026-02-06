#include <bitset>

#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"
#include "frontend/logger.hpp"
#include <format>

// Wisdom Tree detection because it's autistic :(
bool maybe_wisdom_tree(const std::span<const byte_t> rom) {
  if (rom.size() <= 0x8000) return false;

  // scan the first chunk to catch the init code
  const std::size_t limit = std::min<std::size_t>(rom.size(), 0x40000); // 256 KiB

  std::uint32_t ea_total = 0;
  std::uint32_t ea_cart  = 0;
  std::bitset<256> low_bytes{};

  for (std::size_t i = 0; i + 2 < limit; ++i) {
    if (rom[i] != 0xEA) continue; // LD (a16),A

    ++ea_total;
    const auto lo = static_cast<std::uint8_t>(rom[i + 1]);
    const auto hi = static_cast<std::uint8_t>(rom[i + 2]);
    // address = hi<<8 | lo
    if (hi < 0x80) { // 0000-7FFF (cartridge / mapper control area)
      ++ea_cart;
      low_bytes.set(lo);
    }
  }
  const std::size_t distinct_lo = low_bytes.count();

  // Heuristic thresholds:
  // - need some evidence of cart-area stores
  // - need multiple distinct low bytes (since WT bank is low byte of address)
  // - and a decent fraction of EA stores going to cart area
  if (ea_cart < 8) return false;
  if (distinct_lo < 6) return false;

  if (ea_total > 0) {
    const double frac = static_cast<double>(ea_cart) / static_cast<double>(ea_total);
    if (frac < 0.35) return false;
  }

  return true;
}

// Noooo, not you M161 too :(
bool maybe_m161(const std::span<const byte_t> rom) {
  // M161 maps 32 KiB banks into 0000-7FFF, bank number is 3 bits (00-07)
  // So if the ROM is bigger than 32 KiB but doesn't have a whole number of 32 KiB banks, it's likely not M161
  // HOWEVER this is still not very foolproof. Need more research...
  if (rom.size() <= 0x8000) return false;
  if (rom.size() % 0x8000 != 0) return false;          // whole number of 32 KiB banks
  if (rom.size() > 0x8000 * 8) return false;           // max 8 banks

  return true;
}

std::unique_ptr<Mbc> make_mbc(const cart &c) {
  switch (c.header.cartridge_type) {
  case 0x00: {// ROM ONLY (some WT ROMs lie about this, we investigate further
    if (c.rom_size() <= 0x8000) return make_no_mbc(c);  // If strictly <= 32KiB, it's probably safe
    if (maybe_wisdom_tree(c.rom)) {
      Logger::push(
        LogLevel::Warning, "ROM", "Mapper override",
    std::format("{} header type {:02X} looks inconsistent with ROM size {} and appears to be WT; "
                "forcing Wisdom Tree mapper.",
                c.header.title(), c.header.cartridge_type, c.rom_span().size()));
      return make_wisdom_tree(c);
    }
    if (maybe_m161(c.rom)) {
      Logger::push(
        LogLevel::Warning, "ROM", "Mapper override",
    std::format("{} header type {:02X} looks inconsistent with ROM size {} and appears to be M161; "
                "forcing M161 mapper.",
                c.header.title(), c.header.cartridge_type, c.rom_span().size()));
      return make_m161(c);
    }
  }
  case 0x08: // ROM+RAM
  case 0x09: // ROM+RAM+BATTERY
    return make_no_mbc(c);

  case 0x01: // MBC1
  case 0x02: // MBC1+RAM
  case 0x03: // MBC1+RAM+BATTERY
    return make_mbc1(c);

  case 0x05: // MBC2
  case 0x06: // MBC2+BATTERY
    return make_mbc2(c);

  // MMM01 should be correctly detected now with their offset header
  case 0x0B: // MMM01
  case 0x0C: // MMM01+RAM
  case 0x0D: // MMM01+RAM+BATTERY
    return make_mmm01(c);

  case 0x0F: // MBC3+TIMER+BATTERY
  case 0x10: // MBC3+TIMER+RAM+BATTERY
  case 0x11: // MBC3
  case 0x12: // MBC3+RAM
  case 0x13: // MBC3+RAM+BATTERY
    return make_mbc3(c);

  case 0x19: // MBC5
  case 0x1A: // MBC5+RAM
  case 0x1B: // MBC5+RAM+BATTERY
    // EMS
    if (c.header.destination_code == 0xE1 || c.header.title() == "EMSMENU" || c.header.title() == "GB16M")
      return make_ems(c);
  case 0x1C: // MBC5+RUMBLE
  case 0x1D: // MBC5+RUMBLE+RAM
  case 0x1E: // MBC5+RUMBLE+RAM+BATTERY
    return make_mbc5(c);

  case 0x20: // MBC6
    return make_mbc6(c);

  case 0x22:  // MBC7+SENSOR+RUMBLE+RAM+BATTERY
    return make_mbc7(c);

  case 0xC0: // Wisdom Tree, need to check $014A too
    if (c.header.destination_code == 0xD1) return make_wisdom_tree(c);

  case 0xFE: // HuC3
    return make_huc3(c);

  case 0xFF: // HuC1+RAM+BATTERY
    return make_huc1(c);

  default:
    Logger::push(LogLevel::Error, "ROM", "Unknown MBC Type",
      std::format("{} uses an unknown MBC type {:x}, and the ROM cannot be loaded.",
        c.header.title(), c.header.cartridge_type));
    return nullptr;
  }
}
