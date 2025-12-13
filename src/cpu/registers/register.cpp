#include <cpu/registers/register.hpp>
#include <endianness.hpp>
#include <emu_types.hpp>
#include <tuple>

/*
 * Helper for reading 8 bit values from 16 bit registers
 */
static constexpr std::tuple<byte_t, byte_t> split_addr(addr_t value) {
  const byte_t hi =
      static_cast<byte_t>((static_cast<addr_t>(value) >> 8) & 0x00FF);
  const byte_t lo = static_cast<byte_t>(static_cast<addr_t>(value) & 0x00FF);

  if constexpr (is_little_endian()) {
    return {lo, hi};
  }
  return {hi, lo};
}

/*
 * Helper for writing 8 bit values to 16 bit registers
 */
static constexpr addr_t join_addr(byte_t lo, byte_t hi) {
  if constexpr (is_little_endian()) {
    return (static_cast<addr_t>(lo) << 8) | static_cast<addr_t>(hi);
  }
  return (static_cast<addr_t>(hi) << 8) | static_cast<addr_t>(lo);
}

void CpuRegister::write(const addr_t value) {
  std::tie(lo, hi) = split_addr(value);
  full = value;
}

void CpuRegister::write_lo(const byte_t value) {
  const auto [value_lo, value_hi] = split_addr(value);
  full = join_addr(value_lo, hi);
  lo = value_lo;
}

void CpuRegister::write_hi(const byte_t value) {
  const auto [value_lo, value_hi] = split_addr(value);
  full = join_addr(lo, value_hi);
  hi = value_hi;
}

addr_t CpuRegister::read() const { return full; }

byte_t CpuRegister::read_lo() const { return lo; }

byte_t CpuRegister::read_hi() const { return hi; }
