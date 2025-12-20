#ifndef __BITOPS_H
#define __BITOPS_H

#include "cpu/instr/instr.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "memory/bus.hpp"

/*
 * Rotate left with carry
 */
class RLCA : public Instruction {
public:
  RLCA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();

    // Update flags
    const bool carry = (a & 0x80) != 0x00;
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_Z_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    byte_t result = a << 1;
    if (carry)
      result = result | 0x01;

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

/*
 * Rotate right with carry
 */
class RRCA : public Instruction {
public:
  RRCA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();

    // Update flags
    const bool carry = (a & 0x01) != 0x00;
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_Z_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    byte_t result = a >> 1;
    if (carry)
      result = result | 0x80;

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

/*
 * Rotate left through carry
 */
class RLA : public Instruction {
public:
  RLA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const bool old_c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    const bool new_c = (a & 0x80) != 0x00;

    byte_t result = a << 1;
    if (old_c)
      result |= 0x01;

    // Update flags
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_Z_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, new_c);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

/*
 * Rotate right through carry
 */
class RRA : public Instruction {
public:
  RRA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const bool old_c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    const bool new_c = (a & 0x01) != 0x00;

    byte_t result = a >> 1;
    if (old_c)
      result |= 0x80;

    // Update flags
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_Z_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, new_c);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

#endif // __BITOPS_H
