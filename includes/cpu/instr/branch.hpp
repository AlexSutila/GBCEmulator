#ifndef GBC_BRANCH_HPP
#define GBC_BRANCH_HPP

#include "cpu/instr/instr.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "format.hpp"
#include "memory/bus.hpp"

#include <cstdint>

/*
 * Absolute jump
 */
class JP_imm16 final : public Instruction {
public:
  JP_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      lo = bus->read_byte(reg_file->reg_pc++);
      state = InstrStates::INSTR_STATE_READ2;
      break;

    case InstrStates::INSTR_STATE_READ2:
      hi = bus->read_byte(reg_file->reg_pc++);
      reg_file->reg_pc = make_addr(lo, hi);
      break;

    default:
      break;
    }
  }

  std::string describe() override {
    const addr_t imm = make_addr(lo, hi);
    return IroGB::format("JP {}", static_cast<int>(imm));
  }

  std::size_t next_sync_cycle() override {
    return (state == InstrStates::INSTR_STATE_READ) ? 4 : 8;
  }

  InstructionTiming parse() override {
    lo = bus->read_byte(reg_file->reg_pc, false);
    hi = bus->read_byte(reg_file->reg_pc + 1, false);
    state = InstrStates::INSTR_STATE_READ;

    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t lo{}, hi{};
};

/*
 * Jump to address stored in HL
 */
class JP_HL final : public Instruction {
public:
  JP_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override { reg_file->reg_pc = read_reg<Register16Bit::REG_HL>(); }

  std::string describe() override { return IroGB::format("JP HL"); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 4,
        .sync_events = 1,
    };
  }
};

/*
 * Conditional absolute jump
 */
class JP_cond_imm16 final : public Instruction {
public:
  JP_cond_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, StatusFlagMask flag, bool expect)
      : Instruction(reg_file_ptr, bus_ptr), flag(flag), expect(expect) {}

  void exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      lo = bus->read_byte(reg_file->reg_pc++);
      state = InstrStates::INSTR_STATE_READ2;
      break;

    case InstrStates::INSTR_STATE_READ2:
      hi = bus->read_byte(reg_file->reg_pc++);
      if (cond)
        reg_file->reg_pc = make_addr(lo, hi);
      break;

    default:
      break;
    }
  }

  std::string describe() override {
    return IroGB::format("JP {}, {}", to_string(flag, expect), static_cast<int>(make_addr(lo, hi)));
  }

  std::size_t next_sync_cycle() override { return state == InstrStates::INSTR_STATE_READ ? 4 : 8; }

  InstructionTiming parse() override {
    cond = reg_file->reg_af.get_flag(flag) == expect;
    lo = bus->read_byte(reg_file->reg_pc, false);
    hi = bus->read_byte(reg_file->reg_pc + 1, false);
    state = InstrStates::INSTR_STATE_READ;

    const unsigned total_cycles = cond ? 16 : 12;
    return {
        .total_cycles = total_cycles,
        .sync_events = 2,
    };
  }

private:
  const StatusFlagMask flag;
  const bool expect;

  InstrStates state{};
  byte_t lo{}, hi{};
  bool cond{};
};

/*
 * Unconditional relative jump - NOTE: Offset is signed
 */
class JR_imm8 final : public Instruction {
public:
  JR_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override { reg_file->reg_pc += static_cast<addr_t>(imm); }

  std::string describe() override { return IroGB::format("JP {}", static_cast<int>(imm)); }

  InstructionTiming parse() override {
    imm = static_cast<int8_t>(bus->read_byte(reg_file->reg_pc++));
    return {
        .total_cycles = 12,
        .sync_events = 1,
    };
  }

private:
  std::int8_t imm{}; // Signed intentionally
};

/*
 * Conditional relative jump - NOTE: Offset is signed
 */
class JR_cond_imm8 final : public Instruction {
public:
  JR_cond_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, StatusFlagMask flag, bool expect)
      : Instruction(reg_file_ptr, bus_ptr), flag(flag), expect(expect) {}

  void exec() override {
    if (cond)
      reg_file->reg_pc += static_cast<addr_t>(imm);
  }

  std::string describe() override {
    return IroGB::format("JP {}, {}", to_string(flag, expect), static_cast<int>(imm));
  }

  InstructionTiming parse() override {
    cond = reg_file->reg_af.get_flag(flag) == expect;
    imm = static_cast<int8_t>(bus->read_byte(reg_file->reg_pc++));

    const unsigned total_cycles = cond ? 12 : 8;
    return {
        .total_cycles = total_cycles,
        .sync_events = 1,
    };
  }

private:
  const StatusFlagMask flag;
  const bool expect;

  std::int8_t imm{}; // Signed intentionally
  bool cond{};
};

/*
 * Absolute call
 */
class CALL_imm16 final : public Instruction {
public:
  CALL_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      lo = bus->read_byte(reg_file->reg_pc++);
      state = InstrStates::INSTR_STATE_READ2;
      break;

    case InstrStates::INSTR_STATE_READ2:
      hi = bus->read_byte(reg_file->reg_pc++);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    // Write PC to stack and take jump
    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(--sp, static_cast<byte_t>(reg_file->reg_pc >> 8));
      state = InstrStates::INSTR_STATE_WRITE2;
      break;

    case InstrStates::INSTR_STATE_WRITE2:
      bus->write_byte(--sp, static_cast<byte_t>(reg_file->reg_pc & 0xFF));
      reg_file->reg_sp.write(sp);
      reg_file->reg_pc = make_addr(lo, hi);
      break;

    default:
      break;
    }
  }

  std::string describe() override {
    const addr_t imm = make_addr(lo, hi);
    return IroGB::format("CALL {}", static_cast<int>(imm));
  }

  std::size_t next_sync_cycle() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      return 4;
    case InstrStates::INSTR_STATE_READ2:
      return 8;
    case InstrStates::INSTR_STATE_WRITE:
      return 16;
    case InstrStates::INSTR_STATE_WRITE2:
      return 20;
    default:
      return 0; // Never reached
    }
  }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    lo = bus->read_byte(reg_file->reg_pc, false);
    hi = bus->read_byte(reg_file->reg_pc + 1, false);
    sp = reg_file->reg_sp.read();

    return {
        .total_cycles = 24,
        .sync_events = 4,
    };
  }

private:
  InstrStates state{};
  byte_t lo{}, hi{};
  addr_t sp{};
};

/*
 * Conditional absolute call
 */
class CALL_cond_imm16 final : public Instruction {
public:
  CALL_cond_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, StatusFlagMask flag, bool expect)
      : Instruction(reg_file_ptr, bus_ptr), flag(flag), expect(expect) {}
  void exec() override {
    switch (state) {

    case InstrStates::INSTR_STATE_READ:
      lo = bus->read_byte(reg_file->reg_pc++);
      state = InstrStates::INSTR_STATE_READ2;
      break;

    case InstrStates::INSTR_STATE_READ2:
      hi = bus->read_byte(reg_file->reg_pc++);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    // Call is taken, write PC to stack and take jump
    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(--sp, static_cast<byte_t>(reg_file->reg_pc >> 8));
      state = InstrStates::INSTR_STATE_WRITE2;
      break;

    case InstrStates::INSTR_STATE_WRITE2:
      bus->write_byte(--sp, static_cast<byte_t>(reg_file->reg_pc & 0xFF));
      reg_file->reg_pc = make_addr(lo, hi);
      reg_file->reg_sp.write(sp);
      break;

    default:
      break;
    }
  }

  std::string describe() override {
    return IroGB::format("CALL {}, {}", to_string(flag, expect),
                         static_cast<int>(make_addr(lo, hi)));
  }

  std::size_t next_sync_cycle() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      return 4;
    case InstrStates::INSTR_STATE_READ2:
      return 8;
    case InstrStates::INSTR_STATE_WRITE:
      return 16;
    case InstrStates::INSTR_STATE_WRITE2:
      return 20;
    default:
      return 0; // Never reached
    }
  }

  InstructionTiming parse() override {
    cond = reg_file->reg_af.get_flag(flag) == expect;
    lo = bus->read_byte(reg_file->reg_pc, false);
    hi = bus->read_byte(reg_file->reg_pc + 1, false);
    state = InstrStates::INSTR_STATE_READ;
    sp = reg_file->reg_sp.read();

    const unsigned total_cycles = cond ? 24 : 12;
    const unsigned sync_events = cond ? 4 : 2;

    return {
        .total_cycles = total_cycles,
        .sync_events = sync_events,
    };
  }

private:
  const StatusFlagMask flag;
  const bool expect;

  InstrStates state{};
  byte_t lo{}, hi{};
  addr_t sp{};
  bool cond{};
};

/*
 * Unconditional return
 */
class RET final : public Instruction {
public:
  RET(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
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
  }

  std::size_t next_sync_cycle() override { return state == InstrStates::INSTR_STATE_READ ? 4 : 8; }

  std::string describe() override { return IroGB::format("RET"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    sp = reg_file->reg_sp.read();

    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t lo{}, hi{};
  addr_t sp{};
};

/*
 * Conditional return
 */
class RET_cond final : public Instruction {
public:
  RET_cond(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, StatusFlagMask flag, bool expect)
      : Instruction(reg_file_ptr, bus_ptr), flag(flag), expect(expect) {}

  void exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      lo = bus->read_byte(sp++);
      state = InstrStates::INSTR_STATE_READ2;
      break;

    case InstrStates::INSTR_STATE_READ2:
      hi = bus->read_byte(sp++);
      reg_file->reg_sp.write(sp);
      reg_file->reg_pc = lo | (hi << 8);
      break;

    default:
      break;
    }
  }

  std::string describe() override { return IroGB::format("RET {}", to_string(flag, expect)); }

  std::size_t next_sync_cycle() override {
    if (!cond) // Return does not happen
      return 0;
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  InstructionTiming parse() override {
    cond = reg_file->reg_af.get_flag(flag) == expect;
    state = InstrStates::INSTR_STATE_READ;
    sp = reg_file->reg_sp.read();

    const unsigned total_cycles = cond ? 20 : 8;
    const unsigned sync_events = cond ? 2 : 0;

    return {
        .total_cycles = total_cycles,
        .sync_events = sync_events,
    };
  }

private:
  const StatusFlagMask flag;
  const bool expect;

  InstrStates state{};
  byte_t lo{}, hi{};
  addr_t sp{};
  bool cond{};
};

/*
 * Unconditional return, enable interrupts
 */
class RETI final : public Instruction {
public:
  RETI(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, InterruptMasterEnable *ime_ptr)
      : Instruction(reg_file_ptr, bus_ptr), ime(ime_ptr) {}

  void exec() override {
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
  }

  std::size_t next_sync_cycle() override { return state == InstrStates::INSTR_STATE_READ ? 4 : 8; }

  std::string describe() override { return IroGB::format("RETI"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    sp = reg_file->reg_sp.read();

    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InterruptMasterEnable *const ime;
  InstrStates state{};
  byte_t lo{}, hi{};
  addr_t sp{};
};

/*
 * Unconditional jump to reset vector
 */
class RST_vec final : public Instruction {
public:
  RST_vec(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, addr_t vec)
      : Instruction(reg_file_ptr, bus_ptr), vec(vec) {}

  void exec() override {
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
  }

  std::size_t next_sync_cycle() override {
    return state == InstrStates::INSTR_STATE_WRITE ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("RST {}", static_cast<int>(vec)); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_WRITE;
    sp = reg_file->reg_sp.read();

    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  const addr_t vec;

  InstrStates state{};
  addr_t sp{};
};

#endif // GBC_BRANCH_HPP
