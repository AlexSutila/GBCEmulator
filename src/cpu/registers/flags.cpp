#include "cpu/registers/flags.hpp"
#include "emu_types.hpp"

void CpuFlagsRegister::write(const addr_t value) {
  hi = static_cast<byte_t>((value >> 8) & 0x00FF);
  lo = static_cast<byte_t>(value & 0x00FF);

  // Hardware quirk, these bits always read zero so don't set them
  lo = lo & ~0x0F;
}

void CpuFlagsRegister::put_flag(const StatusFlagMask mask, bool value) {
  clr_flag(mask);
  if (value)
    set_flag(mask);
}

bool CpuFlagsRegister::get_flag(const StatusFlagMask mask) const {
  return (lo & static_cast<byte_t>(mask)) != 0;
}

void CpuFlagsRegister::clr_flag(const StatusFlagMask mask) { lo &= ~static_cast<byte_t>(mask); }

void CpuFlagsRegister::set_flag(const StatusFlagMask mask) { lo |= static_cast<byte_t>(mask); }
