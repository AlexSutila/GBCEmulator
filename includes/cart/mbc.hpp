#ifndef GBC_MBC_HPP
#define GBC_MBC_HPP

#pragma once

#include "emu_types.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <span>

struct cart;

class Mbc {
public:
  virtual ~Mbc() = default;

  virtual byte_t read(addr_t addr) = 0;
  virtual void write(addr_t addr, byte_t val) = 0;

  // Elapsed time
  virtual void tick(std::chrono::seconds) {}

  [[nodiscard]] virtual bool has_battery() const noexcept { return false; }
  [[nodiscard]] virtual std::span<const byte_t> ram() const noexcept {
    return {};
  }
  virtual std::span<byte_t> ram() noexcept { return {}; }
};

std::unique_ptr<Mbc> make_mbc(const cart &c);

static constexpr std::size_t kRomBankSize = 0x4000;
static constexpr std::size_t kRamBankSize = 0x2000;

static inline std::size_t rom_bank_count(const std::span<const byte_t> rom) {
  return std::max<std::size_t>(1, rom.size() / kRomBankSize);
}
static inline std::size_t clamp_bank(const std::size_t bank,
                                     const std::size_t count) {
  return (count == 0) ? 0 : (bank % count);
}
static inline byte_t open_bus() { return 0xFF; }

static inline bool type_has_battery(const byte_t t) {
  switch (t) {
  case 0x03: // MBC1+RAM+BATTERY
  case 0x06: // MBC2+BATTERY
  case 0x09: // ROM+RAM+BATTERY (No MBC)
  case 0x0D: // MMM01+RAM+BATTERY
  case 0x0F: // MBC3+TIMER+BATTERY
  case 0x10: // MBC3+TIMER+RAM+BATTERY
  case 0x13: // MBC3+RAM+BATTERY
  case 0x1B: // MBC5+RAM+BATTERY
  case 0x1E: // MBC5+RUMBLE+RAM+BATTERY
  case 0x22: // MBC7+SENSOR+RUMBLE+RAM+BATTERY
  case 0xFF: // HuC1+RAM+BATTERY
    return true;
  default:
    return false;
  }
}

#endif //GBC_MBC_HPP
