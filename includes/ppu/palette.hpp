#ifndef __PALETTE_H
#define __PALETTE_H

#include "emu_types.hpp"
#include <cstdint>

[[nodiscard]] const std::uint32_t get_mono_color(const byte_t idx);

#endif // __PALETTE_H
