#ifndef __REGISTER_H
#define __REGISTER_H

#include <emu_types.hpp>

/*
 * This class is used for registers BC, DE, HL, and SP
 */
class CpuRegister {
public:
  CpuRegister() : lo(0), hi(0) {}
  virtual void write_lo(const byte_t value);
  virtual void write_hi(const byte_t value);
  virtual void write(const addr_t value);
  virtual byte_t read_lo() const;
  virtual byte_t read_hi() const;
  virtual addr_t read() const;

protected:
  byte_t lo, hi;
};

#endif // __REGISTER_H
