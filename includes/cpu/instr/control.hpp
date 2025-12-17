#ifndef __CONTROL_H
#define __CONTROL_H

#include <cpu/instr/instr.hpp>
#include <cpu/registers/regfile.hpp>
#include <emu_types.hpp>
#include <memory/bus.hpp>

/*
 * Complement Accumulator
 */
class CPL : public Instruction {
public:
  CPL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    write_reg<Register8Bit::REG_A>(~a);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_H_MASK);
    return {4, 4};
  }
};

/*
 * Set carry flag
 */
class SCF : public Instruction {
public:
  SCF(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_C_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    return {4, 4};
  }
};

/*
 * Toggle carry flag
 */
class CCF : public Instruction {
public:
  CCF(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const bool c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, !c);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    return {4, 4};
  }
};

/*
 * No operation
 */
class NOP : public Instruction {
public:
  NOP(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override { return {4, 4}; }
};

/*
 * TODO
 */
class DI : public Instruction {
public:
  DI(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override { return {4, 4}; }
};

/*
 * TODO
 */
class EI : public Instruction {
public:
  EI(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override { return {4, 4}; }
};

/*
 * TODO
 */
class HALT : public Instruction {
public:
  HALT(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override { return {4, 4}; }
};

/*
 * TODO
 */
class STOP : public Instruction {
public:
  STOP(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override { return {4, 4}; }
};

#endif // __CONTROL_H
