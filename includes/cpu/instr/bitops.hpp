#ifndef GBC_BITOPS_HPP
#define GBC_BITOPS_HPP

#include "cpu/instr/instr.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "format.hpp"
#include "memory/bus.hpp"

#include <array>
#include <memory>

/*
 * Rotate left with carry
 */
class RLCA final : public Instruction {
public:
  RLCA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
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
    return 4;
  }

  std::string describe() override { return IroGB::format("RLCA"); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 4,
        .sync_events = 1,
    };
  }
};

/*
 * Rotate right with carry
 */
class RRCA final : public Instruction {
public:
  RRCA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
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
    return 4;
  }

  std::string describe() override { return IroGB::format("RRCA"); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 4,
        .sync_events = 1,
    };
  }
};

/*
 * Rotate left through carry
 */
class RLA final : public Instruction {
public:
  RLA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
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
    return 4;
  }

  std::string describe() override { return IroGB::format("RLA"); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 4,
        .sync_events = 1,
    };
  }
};

/*
 * Rotate right through carry
 */
class RRA final : public Instruction {
public:
  RRA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
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
    return 4;
  }

  std::string describe() override { return IroGB::format("RRA"); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 4,
        .sync_events = 1,
    };
  }
};

/*
 * The CB-prefix ISA extension. All instructions below this one lie under this
 * instruction set architecture extension.
 */
class CB_PREFIX final : public Instruction {
public:
  CB_PREFIX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr);
  InstructionTiming parse() override;
  std::size_t exec() override;
  std::size_t mem_access_t_cycle() override;
  std::string describe() override;

private:
  using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
  byte_t op{};

  /* This is identical in behavior to the lookup table in lr35902.hpp */
  void init_cb_prefix(lookup_table_t &lookup_) const;
  lookup_table_t lookup{};
};

class RLC_X final : public Instruction {
public:
  RLC_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  std::size_t exec() override {
    const byte_t x = read_reg(dst);

    const bool carry = (x & 0x80) != 0x00;
    const byte_t result = (x << 1) | (carry ? 0x01 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg(dst, result);
    return 8;
  }

  std::string describe() override { return IroGB::format("RLC {}", to_string(dst)); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
};

class RLC_HL final : public Instruction {
public:
  RLC_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(addr);
      c = (n & 0x80) != 0x00;
      result = (n << 1) | (c ? 0x01 : 0x00);

      // Update flags
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, c);
      state = InstrStates::INSTR_STATE_WRITE;
      break;
    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(addr, result);
      break;
    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("RLC HL"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
  bool c{};
};

class RL_X final : public Instruction {
public:
  RL_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  std::size_t exec() override {
    const byte_t x = read_reg(dst);
    const bool old_c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    const bool new_c = (x & 0x80) != 0x00;
    const byte_t result = (x << 1) | (old_c ? 0x01 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, new_c);

    // Write back
    write_reg(dst, result);
    return 8;
  }

  std::string describe() override { return IroGB::format("RL {}", to_string(dst)); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
};

class RL_HL final : public Instruction {
public:
  RL_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
    const addr_t hl = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(hl);
      c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
      result = (n << 1) | (c ? 0x01 : 0x00);

      // Update flags
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, (n & 0x80) != 0);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(hl, result);
      break;

    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("RL HL"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
  bool c{};
};

class RRC_X final : public Instruction {
public:
  RRC_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  std::size_t exec() override {
    const byte_t x = read_reg(dst);
    const bool carry = (x & 0x01) != 0x00;
    const byte_t result = (x >> 1) | (carry ? 0x80 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg(dst, result);
    return 8;
  }

  std::string describe() override { return IroGB::format("RRC {}", to_string(dst)); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
};

class RRC_HL final : public Instruction {
public:
  RRC_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(addr);
      c = (n & 0x01) != 0x00;
      result = (n >> 1) | (c ? 0x80 : 0x00);

      // Update flags
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, c);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(addr, result);
      break;

    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("RRC HL"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
  bool c{};
};

class RR_X final : public Instruction {
public:
  RR_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  std::size_t exec() override {
    const byte_t x = read_reg(dst);
    const bool old_c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    const bool new_c = (x & 0x01) != 0x00;
    const byte_t result = (x >> 1) | (old_c ? 0x80 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, new_c);

    // Write back
    write_reg(dst, result);
    return 8;
  }

  std::string describe() override { return IroGB::format("RR {}", to_string(dst)); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
};

class RR_HL final : public Instruction {
public:
  RR_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(addr);
      c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
      result = (n >> 1) | (c ? 0x80 : 0x00);

      // Update flags
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, (n & 1) != 0);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(addr, result);
      break;

    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("RR HL"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
  bool c{};
};

class SLA_X final : public Instruction {
public:
  SLA_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  std::size_t exec() override {
    const byte_t x = read_reg(dst);
    const bool carry = (x & 0x80) != 0x00;
    const byte_t result = x << 1;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg(dst, result);
    return 8;
  }

  std::string describe() override { return IroGB::format("SLA {}", to_string(dst)); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
};

class SLA_HL final : public Instruction {
public:
  SLA_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(addr);
      result = n << 1;

      // Update flags
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, (n & 0x80) != 0);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(addr, result);
      break;

    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("SLA HL"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
};

class SRA_X final : public Instruction {
public:
  SRA_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  std::size_t exec() override {
    const byte_t x = read_reg(dst);
    const bool carry = (x & 0x01) != 0x00;
    const byte_t result = (x & 0x80) | (x >> 1);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg(dst, result);
    return 8;
  }

  std::string describe() override { return IroGB::format("SLA {}", to_string(dst)); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
};

class SRA_HL final : public Instruction {
public:
  SRA_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(addr);
      result = (n & 0x80) | (n >> 1);

      // Update flags
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, (n & 1) != 0);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(addr, result);
      break;

    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("SLA HL"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
};

class SWAP_X final : public Instruction {
public:
  SWAP_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  std::size_t exec() override {
    const byte_t x = read_reg(dst);
    const byte_t result = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg(dst, result);
    return 8;
  }

  std::string describe() override { return IroGB::format("SWAP {}", to_string(dst)); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
};

class SWAP_HL final : public Instruction {
public:
  SWAP_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(addr);
      result = ((n & 0xF0) >> 4) | ((n & 0x0F) << 4);

      // Update flags
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(addr, result);
      break;

    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("SWAP HL"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
};

class SRL_X final : public Instruction {
public:
  SRL_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  std::size_t exec() override {
    const byte_t x = read_reg(dst);
    const bool carry = (x & 0x01) != 0x00;
    const byte_t result = x >> 1;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg(dst, result);
    return 8;
  }

  std::string describe() override { return IroGB::format("SRL {}", to_string(dst)); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
};

class SRL_HL final : public Instruction {
public:
  SRL_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(addr);
      result = n >> 1;

      // Update flags
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, (n & 1) != 0);
      state = InstrStates::INSTR_STATE_WRITE;
      break;
    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(addr, result);
      break;
    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("SRL HL"); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
};

class BIT_N_X final : public Instruction {
public:
  BIT_N_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst, byte_t bit)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst), bit(bit) {}

  std::size_t exec() override {
    const byte_t x = read_reg(dst);
    const bool bit_is_zero = ((x >> bit) & 0x01) == 0;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, bit_is_zero);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, true);
    return 8;
  }

  std::string describe() override {
    return IroGB::format("BIT {}, {}", static_cast<int>(bit), to_string(dst));
  }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
  const byte_t bit;
};

class BIT_N_HL final : public Instruction {
public:
  BIT_N_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, byte_t bit)
      : Instruction(reg_file_ptr, bus_ptr), bit(bit) {}

  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t n = bus->read_byte(addr);
    const bool bit_is_zero = ((n >> bit) & 0x01) == 0;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, bit_is_zero);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, true);
    return 12;
  }

  std::string describe() override { return IroGB::format("BIT {}, HL", static_cast<int>(bit)); }
  std::size_t mem_access_t_cycle() override { return 8; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 12,
        .sync_events = 1,
    };
  }

private:
  const byte_t bit;
};

class RES_N_X final : public Instruction {
public:
  RES_N_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst, byte_t bit)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst), bit(bit) {}

  std::size_t exec() override {
    byte_t x = read_reg(dst);
    x &= ~(1 << bit);
    write_reg(dst, x);
    return 8;
  }

  std::string describe() override {
    return IroGB::format("RES {}, {}", static_cast<int>(bit), to_string(dst));
  }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
  const byte_t bit;
};

class RES_N_HL final : public Instruction {
public:
  RES_N_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, byte_t bit)
      : Instruction(reg_file_ptr, bus_ptr), bit(bit) {}

  std::size_t exec() override {
    addr_t hl = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(hl) & ~(1 << bit);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(hl, n);
      break;

    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("RST {}, HL", static_cast<int>(bit)); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t n{};

private:
  const byte_t bit;
};

class SET_N_X final : public Instruction {
public:
  SET_N_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst, byte_t bit)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst), bit(bit) {}

  std::size_t exec() override {
    byte_t x = read_reg(dst);
    x |= (1 << bit);
    write_reg(dst, x);
    return 8;
  }

  std::string describe() override {
    return IroGB::format("SET {}, {}", static_cast<int>(bit), to_string(dst));
  }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
  const byte_t bit;
};

class SET_N_HL final : public Instruction {
public:
  SET_N_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, byte_t bit)
      : Instruction(reg_file_ptr, bus_ptr), bit(bit) {}
  std::size_t exec() override {
    const addr_t hl = read_reg<Register16Bit::REG_HL>();
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(hl) | (1 << bit);
      state = InstrStates::INSTR_STATE_WRITE;
      break;

    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(hl, n);
      break;

    default:
      break;
    }
    return 16;
  }

  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 8 : 12;
  }

  std::string describe() override { return IroGB::format("SET {}, HL", static_cast<int>(bit)); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  InstrStates state{};
  byte_t n{};

private:
  const byte_t bit;
};

#endif // GBC_BITOPS_HPP
