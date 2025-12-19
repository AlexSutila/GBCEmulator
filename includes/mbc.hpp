#ifndef __GBC_MBC_HPP
#define __GBC_MBC_HPP

#pragma once
#include "cart/cart.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>


class Mbc {
public:
    virtual ~Mbc() = default;

    virtual byte_t read(std::uint16_t addr) = 0;
    virtual void   write(std::uint16_t addr, byte_t val) = 0;

    // Elapsed time
    virtual void tick(std::chrono::seconds) {}

    virtual bool has_battery() const noexcept { return false; }
    virtual std::span<const byte_t> ram() const noexcept { return {}; }
    virtual std::span<byte_t>       ram() noexcept { return {}; }
};

std::unique_ptr<Mbc> make_mbc(const cart& c);

static constexpr std::size_t kRomBankSize = 0x4000;
static constexpr std::size_t kRamBankSize = 0x2000;

static std::size_t rom_bank_count(std::span<const byte_t> rom) {
    return std::max<std::size_t>(1, rom.size() / kRomBankSize);
}
static std::size_t clamp_bank(std::size_t bank, std::size_t count) {
    return (count == 0) ? 0 : (bank % count);
}

static byte_t open_bus() { return 0xFF; } // good enough for now, might need change

static bool type_has_battery(byte_t t) {
    switch (t) {
    case 0x03: // MBC1+RAM+BATTERY
    case 0x06: // MBC2+BATTERY
    case 0x09: // ROM+RAM+BATTERY (No MBC)
    case 0x0F: // MBC3+TIMER+BATTERY
    case 0x10: // MBC3+TIMER+RAM+BATTERY
    case 0x13: // MBC3+RAM+BATTERY
    case 0x1B: // MBC5+RAM+BATTERY
    case 0x1E: // MBC5+RUMBLE+RAM+BATTERY
    case 0xFF: // HuC1+RAM+BATTERY (not implemented here)
        return true;
    default:
        return false;
    }
}

#endif //__GBC_MBC_HPP