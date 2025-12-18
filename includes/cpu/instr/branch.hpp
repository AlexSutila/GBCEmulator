#ifndef __BRANCH_H
#define __BRANCH_H

#include "cpu/instr/instr.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "memory/bus.hpp"

#include <cstdint>

/*
 * Absolute jump
 */
class JP_imm16 : public Instruction {
public:
  JP_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    reg_file->reg_pc = imm;
    return {16, 16};
  }
  void parse() override {
    const byte_t lo = bus->read_byte(reg_file->reg_pc++);
    const byte_t hi = bus->read_byte(reg_file->reg_pc++);
    imm = lo | (hi << 8);
  }

private:
  addr_t imm;
};

/*
 * Jump to address stored in HL
 */
class JP_HL : public Instruction {
public:
  JP_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    reg_file->reg_pc = read_reg<Register16Bit::REG_HL>();
    return {4, 4};
  }
};

/*
 * Conditional absolute jump
 */
template <StatusFlagMask flag, bool expect>
class JP_cond_imm16 : public Instruction {
public:
  JP_cond_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const bool cond = reg_file->reg_af.get_flag(flag);
    if (cond != expect)
      return {12, 12};
    reg_file->reg_pc = imm;
    return {16, 16};
  }
  void parse() override {
    const byte_t lo = bus->read_byte(reg_file->reg_pc++);
    const byte_t hi = bus->read_byte(reg_file->reg_pc++);
    imm = lo | (hi << 8);
  }

private:
  addr_t imm;
};

/*
 * Unconditional relative jump - NOTE: Offset is signed
 */
class JR_imm8 : public Instruction {
public:
  JR_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    reg_file->reg_pc += static_cast<addr_t>(imm);
    return {12, 12};
  }
  void parse() override {
    imm = static_cast<int8_t>(bus->read_byte(reg_file->reg_pc++));
  }

private:
  std::int8_t imm; // Signed intentionally
};

/*
 * Conditional relative jump - NOTE: Offset is signed
 */
template <StatusFlagMask flag, bool expect>
class JR_cond_imm8 : public Instruction {
public:
  JR_cond_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const bool cond = reg_file->reg_af.get_flag(flag);
    if (cond != expect)
      return {8, 8};
    reg_file->reg_pc += static_cast<addr_t>(imm);
    return {12, 12};
  }
  void parse() override {
    imm = static_cast<int8_t>(bus->read_byte(reg_file->reg_pc++));
  }

private:
  std::int8_t imm; // Signed intentionally
};

/*
 * Absolute call
 */
class CALL_imm16 : public Instruction {
public:
  CALL_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    addr_t sp = reg_file->reg_sp.read();
    addr_t pc = reg_file->reg_pc;

    // Push current PC onto the stack (high byte first)
    bus->write_byte(--sp, static_cast<byte_t>(pc >> 8));
    bus->write_byte(--sp, static_cast<byte_t>(pc & 0xFF));

    // Write back for updated stack pointer
    reg_file->reg_sp.write(sp);
    reg_file->reg_pc = imm;
    return {24, 24};
  }
  void parse() override {
    const byte_t lo = bus->read_byte(reg_file->reg_pc++);
    const byte_t hi = bus->read_byte(reg_file->reg_pc++);
    imm = lo | (hi << 8);
  }

private:
  addr_t imm;
};

/*
 * Conditional absolute call
 */
template <StatusFlagMask flag, bool expect>
class CALL_cond_imm16 : public Instruction {
public:
  CALL_cond_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const bool cond = reg_file->reg_af.get_flag(flag);
    if (cond != expect)
      return {12, 12};
    addr_t sp = reg_file->reg_sp.read();
    addr_t pc = reg_file->reg_pc;

    // Push current PC onto the stack (high byte first)
    bus->write_byte(--sp, static_cast<byte_t>(pc >> 8));
    bus->write_byte(--sp, static_cast<byte_t>(pc & 0xFF));

    // Write back for updated stack pointer
    reg_file->reg_sp.write(sp);
    reg_file->reg_pc = imm;
    return {24, 24};
  }
  void parse() override {
    const byte_t lo = bus->read_byte(reg_file->reg_pc++);
    const byte_t hi = bus->read_byte(reg_file->reg_pc++);
    imm = lo | (hi << 8);
  }

private:
  addr_t imm;
};

/*
 * Unconditional return
 */
class RET : public Instruction {
public:
  RET(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    addr_t sp = reg_file->reg_sp.read();
    const addr_t lo = bus->read_byte(sp++);
    const addr_t hi = bus->read_byte(sp++);

    // Write back for updated stack pointer
    reg_file->reg_sp.write(sp);
    reg_file->reg_pc = lo | (hi << 8);
    return {16, 16};
  }
};

/*
 * Unconditional return
 */
template <StatusFlagMask flag, bool expect>
class RET_cond : public Instruction {
public:
  RET_cond(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const bool cond = reg_file->reg_af.get_flag(flag);
    if (cond != expect)
      return {8, 8};
    addr_t sp = reg_file->reg_sp.read();
    const addr_t lo = bus->read_byte(sp++);
    const addr_t hi = bus->read_byte(sp++);

    // Write back for updated stack pointer
    reg_file->reg_sp.write(sp);
    reg_file->reg_pc = lo | (hi << 8);
    return {20, 20};
  }
};

/*
 * Unconditional return, enable interrupts
 */
class RETI : public Instruction {
public:
  RETI(RegisterFile *reg_file_ptr, AddressBus *bus_ptr,
       InterruptMasterEnable *ime_ptr)
      : Instruction(reg_file_ptr, bus_ptr), ime(ime_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    addr_t sp = reg_file->reg_sp.read();
    const addr_t lo = bus->read_byte(sp++);
    const addr_t hi = bus->read_byte(sp++);

    // Enable IME, but effects are instant bc of hardware quirk
    ime->enable(false);

    // Write back for updated stack pointer
    reg_file->reg_sp.write(sp);
    reg_file->reg_pc = lo | (hi << 8);
    return {16, 16};
  }

private:
  InterruptMasterEnable *const ime;
};

/*
 * Unconditional jump to reset vector
 */
template <addr_t vec> class RST_vec : public Instruction {
public:
  RST_vec(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}

  std::tuple<std::size_t, std::size_t> step() override {
    const addr_t ret = reg_file->reg_pc;
    addr_t sp = reg_file->reg_sp.read();
    bus->write_byte(--sp, static_cast<uint8_t>(ret >> 8));
    bus->write_byte(--sp, static_cast<uint8_t>(ret & 0xFF));

    // Write back for updated stack pointer
    reg_file->reg_sp.write(sp);
    reg_file->reg_pc = vec;
    return {16, 16};
  }
};

#endif // __BRANCH_H
