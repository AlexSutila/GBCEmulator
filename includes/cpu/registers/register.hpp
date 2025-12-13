#ifndef __REGISTER_H
#define __REGISTER_H

#include <emu_types.hpp>

/*
 * LR35902 Register Set is as follows, where each register is
 * sixteen bits. Registers can be used as either full sixteen
 * bit registers, or two eight bit registers.
 *
 * 16-bit | Hi | Lo | Name / Function
 * -------+----+----+-------------------------
 * AF     | A  | -  | Accumulator & Flags
 * BC     | B  | C  | BC
 * DE     | D  | E  | DE
 * HL     | H  | L  | HL
 * SP     | -  | -  | Stack Pointer
 * PC     | -  | -  | Program Counter / Pointer
 *
 * This class is used for registers BC, DE, and HL
 */
class CpuRegister {
public:
  CpuRegister() : full(0), lo(0), hi(0) {}
  virtual void write_lo(const byte_t value);
  virtual void write_hi(const byte_t value);
  virtual void write(const addr_t value);
  virtual byte_t read_lo() const;
  virtual byte_t read_hi() const;
  virtual addr_t read() const;

private:
  byte_t lo, hi;
  addr_t full;
};

#endif // __REGISTER_H
