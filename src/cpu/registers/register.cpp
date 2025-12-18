#include "cpu/registers/register.hpp"
#include "emu_types.hpp"

void CpuRegister::write(const addr_t value) {
  hi = static_cast<byte_t>((value >> 8) & 0x00FF);
  lo = static_cast<byte_t>(value & 0x00FF);
}

void CpuRegister::write_lo(const byte_t value) { lo = value; }

void CpuRegister::write_hi(const byte_t value) { hi = value; }

addr_t CpuRegister::read() const {
  return (static_cast<addr_t>(hi) << 8) | static_cast<addr_t>(lo);
}

byte_t CpuRegister::read_lo() const { return lo; }

byte_t CpuRegister::read_hi() const { return hi; }
