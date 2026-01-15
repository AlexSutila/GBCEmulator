#ifndef __CONTROL_H
#define __CONTROL_H

#include "cpu/instr/instr.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "gbc.hpp"
#include "memory/bus.hpp"
#include <format>

/*
 * Complement Accumulator
 */
class CPL final : public Instruction {
public:
  CPL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    write_reg<Register8Bit::REG_A>(~a);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_H_MASK);
    return 4;
  }
  std::string describe() override { return std::format("CPL"); }
};

/*
 * Set carry flag
 */
class SCF final : public Instruction {
public:
  SCF(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_C_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    return 4;
  }
  std::string describe() override { return std::format("SCF"); }
};

/*
 * Toggle carry flag
 */
class CCF final : public Instruction {
public:
  CCF(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const bool c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, !c);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    return 4;
  }
  std::string describe() override { return std::format("CCF"); }
};

/*
 * No operation
 */
class NOP final : public Instruction {
public:
  NOP(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::string describe() override { return std::format("NOP"); }
  std::size_t exec() override { return 4; }
};

/*
 * Disable interrupts
 */
class DI final : public Instruction {
public:
  DI(RegisterFile *reg_file_ptr, AddressBus *bus_ptr,
     InterruptMasterEnable *ime_ptr)
      : Instruction(reg_file_ptr, bus_ptr), ime(ime_ptr) {}
  std::size_t exec() override {
    ime->disable();
    return 4;
  }
  std::string describe() override { return std::format("DI"); }

private:
  InterruptMasterEnable *const ime;
};

/*
 * Enable interrupts
 */
class EI final : public Instruction {
public:
  EI(RegisterFile *reg_file_ptr, AddressBus *bus_ptr,
     InterruptMasterEnable *ime_ptr)
      : Instruction(reg_file_ptr, bus_ptr), ime(ime_ptr) {}
  std::size_t exec() override {
    ime->enable(true);
    return 4;
  }
  std::string describe() override { return std::format("EI"); }

private:
  InterruptMasterEnable *const ime;
};

/*
 * TODO: Halt processor
 */
class HALT final : public Instruction {
public:
  HALT(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, runtime_sys_info &sys)
      : Instruction(reg_file_ptr, bus_ptr), sys_(sys) {}
  std::size_t exec() override {
    sys_.halted = true;
    return 4;
  }
  std::string describe() override { return std::format("HALT"); }

private:
  runtime_sys_info &sys_;
};

/*
 * TODO: Who knows honestly lmao. Need to research this instruction
 */
class STOP final : public Instruction {
public:
  STOP(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, runtime_sys_info &sys)
      : Instruction(reg_file_ptr, bus_ptr), sys_(sys) {}
  std::size_t exec() override { return 4; }
  std::string describe() override { return std::format("STOP"); }

private:
  runtime_sys_info &sys_;
};

#endif // __CONTROL_H
