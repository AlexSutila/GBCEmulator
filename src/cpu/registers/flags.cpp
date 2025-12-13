#include <cpu/registers/flags.hpp>
#include <endianness.hpp>
#include <emu_types.hpp>

bool CpuFlagsRegister::get_flag(const StatusFlagMask mask) const {
  return (lo & static_cast<byte_t>(mask)) != 0;
}

void CpuFlagsRegister::clr_flag(const StatusFlagMask mask) {
  lo &= ~static_cast<byte_t>(mask);
}

void CpuFlagsRegister::set_flag(const StatusFlagMask mask) {
  lo |= static_cast<byte_t>(mask);
}
