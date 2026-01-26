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
 * Halt processor, though this instruction is quite buggy under certain hardware
 * conditions, hence we kinda have to pass a million things to it to emulate
 * these hardware quirks as well.
 */
class HALT final : public Instruction {
public:
  HALT(RegisterFile *reg_file_ptr, AddressBus *bus_ptr,
       InterruptMasterEnable &ime, InterruptBits &if_reg, InterruptBits &ie_reg,
       runtime_sys_info &sys)
      : Instruction(reg_file_ptr, bus_ptr),
        ime_(ime),   // Needed to trigger the HALT bug
        if_(if_reg), // ^^^
        ie_(ie_reg), // ^^^
        sys_(sys) {}
  std::size_t exec() override {
    constexpr byte_t mask = 0x1F; // Mask out unused interrupt bits
    const byte_t isr_pending = if_.peek() & ie_.peek() & mask;
    sys_.halted = true;

    /* If the IME is disabled and there is no interrupt pending, there is a
     * hardware bug that causes PC increment to fail for one instruction */
    if (!ime_.is_enabled() && !isr_pending)
      reg_file->halt_bug_triggered = true;
    return 4;
  }
  std::string describe() override { return std::format("HALT"); }

  /* NOTE: This instruction does not access memory. However, we still do not
   * want this instruction to take effect and actually place the processor in
   * HALT mode until the instruction has completed. */
  std::size_t mem_access_t_cycle() override { return 4; }

private:
  InterruptMasterEnable &ime_;
  InterruptBits &if_, &ie_;
  runtime_sys_info &sys_;
};

/*
 * This instruction is... bizzare. The most important thing is that it is used
 * to switch into double speed mode.
 *
 * TODO: Implement bizzare behavior from that flow chart... it sucks lol
 */
class STOP final : public Instruction {
public:
  STOP(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, runtime_sys_info &sys)
      : Instruction(reg_file_ptr, bus_ptr), sys_(sys) {}
  std::size_t exec() override {
    if (sys_.speed_switch_armed) {
      sys_.double_speed = !sys_.double_speed;
      sys_.speed_switch_armed = false;
    }
    return 4;
  }
  std::string describe() override { return std::format("STOP"); }

  // Subject to change??? But same rationale as HALT timing for now.
  std::size_t mem_access_t_cycle() override { return 4; }

private:
  runtime_sys_info &sys_;
};

#endif // __CONTROL_H
