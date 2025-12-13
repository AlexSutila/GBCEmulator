#ifndef __FLAGS_H
#define __FLAGS_H

#include <cpu/registers/register.hpp>
#include <emu_types.hpp>

/*
 * The F register allocates it's four most siginificant bits
 * for status flags that can also be read and written.
 *
 * Bit | Name | Explanation
 * ----+------+------------------------------
 * 7   | z    | Zero flag
 * 6   | n    | Subtraction flag (BCD)
 * 5   | h    | Half Carry flag (BCD)
 * 4   | c    | Carry flag
 *
 */

enum class StatusFlagMask : byte_t {
  FLAG_Z_MASK = 1u << 4,
  FLAG_N_MASK = 1u << 5,
  FLAG_H_MASK = 1u << 6,
  FLAG_C_MASK = 1u << 7,
};

/*
 * Used only for AF register
 */
class CpuFlagsRegister : public CpuRegister {
public:
  CpuFlagsRegister() : full(0), lo(0), hi(0) {}

  /* For status flag specific operations */
  bool get_flag(const StatusFlagMask mask) const;
  void clr_flag(const StatusFlagMask mask);
  void set_flag(const StatusFlagMask mask);

private:
  byte_t lo, hi;
  addr_t full;
};

#endif // __FLAGS_H
