#ifndef __BRANCH_H
#define __BRANCH_H

#include "cpu/instr/instr.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "memory/bus.hpp"

#include <cstdint>
#include <format>

/*
 * Absolute jump
 */
class JP_imm16 final : public Instruction {
public:
  JP_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    reg_file->reg_pc = imm;
    return 16;
  }
  std::string describe() override {
    return std::format("JP {}", static_cast<int>(imm));
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
class JP_HL final : public Instruction {
public:
  JP_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    reg_file->reg_pc = read_reg<Register16Bit::REG_HL>();
    return 4;
  }
  std::string describe() override { return std::format("JP HL"); }
};

/*
 * Conditional absolute jump
 */
template <StatusFlagMask flag, bool expect>
class JP_cond_imm16 final : public Instruction {
public:
  JP_cond_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const bool cond = reg_file->reg_af.get_flag(flag);
    if (cond != expect)
      return 12;
    reg_file->reg_pc = imm;
    return 16;
  }
  std::string describe() override {
    return std::format("JP {}, {}", to_string<flag, expect>(),
                       static_cast<int>(imm));
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
class JR_imm8 final : public Instruction {
public:
  JR_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    reg_file->reg_pc += static_cast<addr_t>(imm);
    return 12;
  }
  std::string describe() override {
    return std::format("JP {}", static_cast<int>(imm));
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
class JR_cond_imm8 final : public Instruction {
public:
  JR_cond_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const bool cond = reg_file->reg_af.get_flag(flag);
    if (cond != expect)
      return 8;
    reg_file->reg_pc += static_cast<addr_t>(imm);
    return 12;
  }
  std::string describe() override {
    return std::format("JP {}, {}", to_string<flag, expect>(),
                       static_cast<int>(imm));
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
class CALL_imm16 final : public Instruction {
public:
  CALL_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    addr_t sp = reg_file->reg_sp.read();
    addr_t pc = reg_file->reg_pc;

    // Push current PC onto the stack (high byte first)
    bus->write_byte(--sp, static_cast<byte_t>(pc >> 8));
    bus->write_byte(--sp, static_cast<byte_t>(pc & 0xFF));

    // Write back for updated stack pointer
    reg_file->reg_sp.write(sp);
    reg_file->reg_pc = imm;
    return 24;
  }
  std::string describe() override {
    return std::format("CALL {}", static_cast<int>(imm));
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
class CALL_cond_imm16 final : public Instruction {
public:
  CALL_cond_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const bool cond = reg_file->reg_af.get_flag(flag);
    if (cond != expect)
      return 12;
    addr_t sp = reg_file->reg_sp.read();
    addr_t pc = reg_file->reg_pc;

    // Push current PC onto the stack (high byte first)
    bus->write_byte(--sp, static_cast<byte_t>(pc >> 8));
    bus->write_byte(--sp, static_cast<byte_t>(pc & 0xFF));

    // Write back for updated stack pointer
    reg_file->reg_sp.write(sp);
    reg_file->reg_pc = imm;
    return 24;
  }
  std::string describe() override {
    return std::format("CALL {}, {}", to_string<flag, expect>(),
                       static_cast<int>(imm));
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
class RET final : public Instruction {
public:
  RET(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      lo = bus->read_byte(sp++);
      state = InstrStates::INSTR_STATE_READ2;
      break;
    case InstrStates::INSTR_STATE_READ2:
      hi = bus->read_byte(sp++);
      reg_file->reg_pc = lo | (hi << 8);
      reg_file->reg_sp.write(sp);
      break;
    default:
      break;
    }
    return 16;
  }
  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 4 : 8;
  }
  void parse() override {
    state = InstrStates::INSTR_STATE_READ;
    sp = reg_file->reg_sp.read();
  }
  std::string describe() override { return std::format("RET"); }

private:
  InstrStates state{};
  byte_t lo{}, hi{};
  addr_t sp{};
};

/*
 * Unconditional return
 */
template <StatusFlagMask flag, bool expect>
class RET_cond final : public Instruction {
public:
  RET_cond(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const bool cond = reg_file->reg_af.get_flag(flag);
    if (cond != expect)
      return 8;
    addr_t sp = reg_file->reg_sp.read();
    const addr_t lo = bus->read_byte(sp++);
    const addr_t hi = bus->read_byte(sp++);

    // Write back for updated stack pointer
    reg_file->reg_sp.write(sp);
    reg_file->reg_pc = lo | (hi << 8);
    return 20;
  }
  std::string describe() override {
    return std::format("RET {}", to_string<flag, expect>());
  }
};

/*
 * Unconditional return, enable interrupts
 */
class RETI final : public Instruction {
public:
  RETI(RegisterFile *reg_file_ptr, AddressBus *bus_ptr,
       InterruptMasterEnable *ime_ptr)
      : Instruction(reg_file_ptr, bus_ptr), ime(ime_ptr) {}
  std::size_t exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      lo = bus->read_byte(sp++);
      state = InstrStates::INSTR_STATE_READ2;
      break;
    case InstrStates::INSTR_STATE_READ2:
      hi = bus->read_byte(sp++);
      reg_file->reg_pc = lo | (hi << 8);
      reg_file->reg_sp.write(sp);

      // Enable IME, but effects are instant bc of hardware quirk
      ime->enable(false);
    default:
      break;
    }
    return 16;
  }
  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 4 : 8;
  }
  void parse() override {
    state = InstrStates::INSTR_STATE_READ;
    sp = reg_file->reg_sp.read();
  }
  std::string describe() override { return std::format("RETI"); }

private:
  InterruptMasterEnable *const ime;
  InstrStates state{};
  byte_t lo{}, hi{};
  addr_t sp{};
};

/*
 * Unconditional jump to reset vector
 */
template <addr_t vec> class RST_vec final : public Instruction {
public:
  RST_vec(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(--sp, static_cast<uint8_t>(reg_file->reg_pc >> 8));
      state = InstrStates::INSTR_STATE_WRITE2;
      break;
    case InstrStates::INSTR_STATE_WRITE2:
      bus->write_byte(--sp, static_cast<uint8_t>(reg_file->reg_pc & 0xFF));
      reg_file->reg_sp.write(sp);
      reg_file->reg_pc = vec;
      break;
    default:
      break;
    }
    return 16;
  }
  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_WRITE ? 8 : 12;
  }
  std::string describe() override {
    return std::format("RST {}", static_cast<int>(vec));
  }
  void parse() override {
    state = InstrStates::INSTR_STATE_WRITE;
    sp = reg_file->reg_sp.read();
  }

private:
  InstrStates state{};
  addr_t sp{};
};

#endif // __BRANCH_H
