#include "cart/cart.hpp"
#include "cart/mbc.hpp"
#include "cart/mbc_creator.hpp"

std::unique_ptr<Mbc> make_mbc(const cart &c) {
  switch (c.header.cartridge_type) {
  case 0x00: // ROM ONLY
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

  case 0x0F: // MBC3+TIMER+BATTERY
  case 0x10: // MBC3+TIMER+RAM+BATTERY
  case 0x11: // MBC3
  case 0x12: // MBC3+RAM
  case 0x13: // MBC3+RAM+BATTERY
    return make_mbc3(c);

  case 0x19: // MBC5
  case 0x1A: // MBC5+RAM
  case 0x1B: // MBC5+RAM+BATTERY
  case 0x1C: // MBC5+RUMBLE
  case 0x1D: // MBC5+RUMBLE+RAM
  case 0x1E: // MBC5+RUMBLE+RAM+BATTERY
    return make_mbc5(c);
  default:
    throw std::runtime_error("Unsupported cartridge type (mapper): " +
                             std::to_string(c.header.cartridge_type));
  }
}
