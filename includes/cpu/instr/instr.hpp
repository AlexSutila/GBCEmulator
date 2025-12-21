#ifndef __INSTR_H
#define __INSTR_H

#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "memory/bus.hpp"
#include <cstddef>

class Instruction {
public:
  Instruction(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : reg_file(reg_file_ptr), bus(bus_ptr) {}

  /**
   * Executes the instruction in full, to be called on the memory access
   * clock cycle when appropriate.
   *
   * @return A tuple containing:
   *  - size_t: total number of clock cycles for this instruction
   */
  virtual std::size_t exec() = 0;

  /**
   * @return A tuple containing:
   *  - size_t: the memory access clock cycle of the instruction, when
   *    applicable. If memory access timing does not matter, use zero.
   */
  virtual std::size_t mem_access_t_cycle() { return 0; };

  /**
   * Parses the instruction in it's entirety, reading intermediate fields
   */
  virtual void parse() {}

protected:
  /*
   * All just compile time stuff to reduce having to go through unnecessry
   * decode logic during runtime. A lot of it can be done during compile time
   * unless an instruction deals with immediate values.
   */

  template <Register16Bit reg> inline void write_reg(addr_t addr) const {
    if constexpr (reg == Register16Bit::REG_AF)
      reg_file->reg_af.write(addr);
    else if constexpr (reg == Register16Bit::REG_BC)
      reg_file->reg_bc.write(addr);
    else if constexpr (reg == Register16Bit::REG_DE)
      reg_file->reg_de.write(addr);
    else if constexpr (reg == Register16Bit::REG_HL)
      reg_file->reg_hl.write(addr);
    else if constexpr (reg == Register16Bit::REG_SP)
      reg_file->reg_sp.write(addr);
    else
      static_assert("Invalid 16-bit register");
  }

  template <Register16Bit reg> addr_t inline read_reg() const {
    if constexpr (reg == Register16Bit::REG_AF)
      return reg_file->reg_af.read();
    else if constexpr (reg == Register16Bit::REG_BC)
      return reg_file->reg_bc.read();
    else if constexpr (reg == Register16Bit::REG_DE)
      return reg_file->reg_de.read();
    else if constexpr (reg == Register16Bit::REG_HL)
      return reg_file->reg_hl.read();
    else if constexpr (reg == Register16Bit::REG_SP)
      return reg_file->reg_sp.read();
    else
      static_assert("Invalid 16-bit register");
  }

  template <Register8Bit reg> inline void write_reg(byte_t byte) const {
    if constexpr (reg == Register8Bit::REG_A)
      reg_file->reg_af.write_hi(byte);
    else if constexpr (reg == Register8Bit::REG_F)
      reg_file->reg_af.write_lo(byte);
    else if constexpr (reg == Register8Bit::REG_B)
      reg_file->reg_bc.write_hi(byte);
    else if constexpr (reg == Register8Bit::REG_C)
      reg_file->reg_bc.write_lo(byte);
    else if constexpr (reg == Register8Bit::REG_D)
      reg_file->reg_de.write_hi(byte);
    else if constexpr (reg == Register8Bit::REG_E)
      reg_file->reg_de.write_lo(byte);
    else if constexpr (reg == Register8Bit::REG_H)
      reg_file->reg_hl.write_hi(byte);
    else if constexpr (reg == Register8Bit::REG_L)
      reg_file->reg_hl.write_lo(byte);
    else
      static_assert("Invalid 8-bit register");
  }

  template <Register8Bit reg> byte_t inline read_reg() const {
    if constexpr (reg == Register8Bit::REG_A)
      return reg_file->reg_af.read_hi();
    else if constexpr (reg == Register8Bit::REG_F)
      return reg_file->reg_af.read_lo();
    else if constexpr (reg == Register8Bit::REG_B)
      return reg_file->reg_bc.read_hi();
    else if constexpr (reg == Register8Bit::REG_C)
      return reg_file->reg_bc.read_lo();
    else if constexpr (reg == Register8Bit::REG_D)
      return reg_file->reg_de.read_hi();
    else if constexpr (reg == Register8Bit::REG_E)
      return reg_file->reg_de.read_lo();
    else if constexpr (reg == Register8Bit::REG_H)
      return reg_file->reg_hl.read_hi();
    else if constexpr (reg == Register8Bit::REG_L)
      return reg_file->reg_hl.read_lo();
    else
      static_assert("Invalid 8-bit register");
  }

  RegisterFile *const reg_file;
  AddressBus *const bus;
};

#endif // __INSTR_H
