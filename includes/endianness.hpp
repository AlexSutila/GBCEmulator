#ifndef __ENDIANNESS_H
#define __ENDIANNESS_H

#include <bit>

enum Endianness {
  LITTLE_ENDIAN,
  BIG_ENDIAN,
};

constexpr Endianness get_endianness() {
  if (std::endian::native == std::endian::little)
    return LITTLE_ENDIAN;
  return BIG_ENDIAN;
};

constexpr bool is_little_endian() {
  return std::endian::native == std::endian::little;
}

constexpr bool is_big_endian() {
  return std::endian::native == std::endian::big;
}

#endif // __ENDIANNESS_H
