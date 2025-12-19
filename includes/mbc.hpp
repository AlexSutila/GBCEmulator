#ifndef __GBC_MBC_HPP
#define __GBC_MBC_HPP

#pragma once
#include "cart/cart.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
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


#endif //__GBC_MBC_HPP