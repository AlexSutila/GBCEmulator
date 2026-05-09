#ifndef GBC_MOVES_HPP
#define GBC_MOVES_HPP

#include "cpu/instr/instr.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "format.hpp"
#include "memory/bus.hpp"

/*
 * Copies value from Y into X, operates only on 8-bit registers
 */
class LD_X_Y final : public Instruction {
public:
  LD_X_Y(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst, Register8Bit src)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst), src(src) {}

  void exec() override {
    const byte_t val = read_reg(src);
    write_reg(dst, val);
  }

  std::string describe() override {
    return IroGB::format("LD {}, {}", to_string(dst), to_string(src));
  }

  InstructionTiming parse() override {
    return {
        .total_cycles = 4,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
  const Register8Bit src;
};

/*
 * Copies immediate value into X, operates only on 8-bit registers
 */
class LD_X_imm8 final : public Instruction {
public:
  LD_X_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  void exec() override { write_reg(dst, imm); }

  std::string describe() override {
    return IroGB::format("LD {}, {}", to_string(dst), static_cast<int>(imm));
  }

  InstructionTiming parse() override {
    imm = bus->read_byte(reg_file->reg_pc++);
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
  byte_t imm{};
};

/*
 * Copies byte read from address HL into X, operates only on 8-bit registers
 */
class LD_X_HL final : public Instruction {
public:
  LD_X_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  void exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t mem_byte = bus->read_byte(addr);
    write_reg(dst, mem_byte);
  }

  std::string describe() override { return IroGB::format("LD {}, HL", to_string(dst)); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit dst;
};

/*
 * Copies byte from X into address HL, operates only on 8-bit registers
 */
class LD_HL_X final : public Instruction {
public:
  LD_HL_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register8Bit src)
      : Instruction(reg_file_ptr, bus_ptr), src(src) {}

  void exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t reg_byte = read_reg(src);
    bus->write_byte(addr, reg_byte);
  }

  std::string describe() override { return IroGB::format("LD {}, HL", to_string(src)); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register8Bit src;
};

/*
 * Copies 8-bit immediate value into address HL
 */
class LD_HL_imm8 final : public Instruction {
public:
  LD_HL_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    bus->write_byte(addr, imm);
  }

  std::string describe() override { return IroGB::format("LD HL, {}", static_cast<int>(imm)); }
  std::size_t next_sync_cycle() override { return 8; }

  InstructionTiming parse() override {
    imm = bus->read_byte(reg_file->reg_pc++);
    return {
        .total_cycles = 12,
        .sync_events = 1,
    };
  }

private:
  byte_t imm{};
};

/*
 * Copies 8-bit value from address specified by register XX into A
 */
class LD_A_XX final : public Instruction {
public:
  LD_A_XX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register16Bit src)
      : Instruction(reg_file_ptr, bus_ptr), src(src) {}

  void exec() override {
    const addr_t addr = read_reg(src);
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);
  }

  std::string describe() override { return IroGB::format("LD A, {}", to_string(src)); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register16Bit src;
};

/*
 * Copies 8-bit value from immediate address value into A
 */
class LD_A_imm16 final : public Instruction {
public:
  LD_A_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);
  }

  std::string describe() override { return IroGB::format("LD A, {}", static_cast<int>(addr)); }
  std::size_t next_sync_cycle() override { return 12; }

  InstructionTiming parse() override {
    const byte_t lsb = bus->read_byte(reg_file->reg_pc++);
    const byte_t msb = bus->read_byte(reg_file->reg_pc++);
    addr = (msb << 8) | lsb;

    return {
        .total_cycles = 16,
        .sync_events = 1,
    };
  }

private:
  addr_t addr{};
};

/*
 * Copies A into address specified by 16-bit register XX
 */
class LD_XX_A final : public Instruction {
public:
  LD_XX_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register16Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  void exec() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    const addr_t addr = read_reg(dst);
    bus->write_byte(addr, reg_byte);
  }

  std::string describe() override { return IroGB::format("LD {}, A", to_string(dst)); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }

private:
  const Register16Bit dst;
};

/*
 * Copies A into address specified by 16-bit immediate value
 */
class LD_imm16_A final : public Instruction {
public:
  LD_imm16_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    bus->write_byte(addr, reg_byte);
  }

  std::string describe() override { return IroGB::format("LD {}, A", static_cast<int>(addr)); }
  std::size_t next_sync_cycle() override { return 12; }

  InstructionTiming parse() override {
    const byte_t lsb = bus->read_byte(reg_file->reg_pc++);
    const byte_t msb = bus->read_byte(reg_file->reg_pc++);
    addr = (msb << 8) | lsb;

    return {
        .total_cycles = 16,
        .sync_events = 1,
    };
  }

private:
  addr_t addr{};
};

/*
 * Copy IO-register specified by 8-bit immediate into A
 */
class LDH_A_imm8 final : public Instruction {
public:
  LDH_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);
  }

  std::string describe() override { return IroGB::format("LD A, {}", static_cast<int>(addr)); }
  std::size_t next_sync_cycle() override { return 8; }

  InstructionTiming parse() override {
    addr = 0xFF00 | bus->read_byte(reg_file->reg_pc++);
    return {
        .total_cycles = 12,
        .sync_events = 1,
    };
  }

private:
  addr_t addr{};
};

/*
 * Copy A into IO-register specified by 8-bit immediate
 */
class LDH_imm8_A final : public Instruction {
public:
  LDH_imm8_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    bus->write_byte(addr, reg_byte);
  }

  std::string describe() override { return IroGB::format("LD {}, A", static_cast<int>(addr)); }
  std::size_t next_sync_cycle() override { return 8; }

  InstructionTiming parse() override {
    addr = 0xFF00 | bus->read_byte(reg_file->reg_pc++);
    return {
        .total_cycles = 12,
        .sync_events = 1,
    };
  }

private:
  addr_t addr{};
};

/*
 * Copy A into IO-register specified by register C
 */
class LDH_C_A final : public Instruction {
public:
  LDH_C_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    bus->write_byte(0xFF00 | read_reg<Register8Bit::REG_C>(), reg_byte);
  }

  std::string describe() override { return IroGB::format("LD C, A"); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }
};

/*
 * Copy IO-register specified by register C into A
 */
class LDH_A_C final : public Instruction {
public:
  LDH_A_C(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const byte_t mem_byte = bus->read_byte(0xFF00 | read_reg<Register8Bit::REG_C>());
    reg_file->reg_af.write_hi(mem_byte);
  }

  std::string describe() override { return IroGB::format("LDH A, C"); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }
};

/*
 * Copy A into address specified by HL, increment HL
 */
class LDI_HL_A final : public Instruction {
public:
  LDI_HL_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    bus->write_byte(addr, reg_byte);

    /* Increments address stored in HL */
    write_reg<Register16Bit::REG_HL>(addr + 1);
  }

  std::string describe() override { return IroGB::format("LDI HL, A"); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }
};

/*
 * Copy from address specified by HL into A, increment HL
 */
class LDI_A_HL final : public Instruction {
public:
  LDI_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);

    /* Increments address stored in HL */
    write_reg<Register16Bit::REG_HL>(addr + 1);
  }

  std::string describe() override { return IroGB::format("LDI A, HL"); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }
};

/*
 * Copy A into address specified by HL, decrement HL
 */
class LDD_HL_A final : public Instruction {
public:
  LDD_HL_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    bus->write_byte(addr, reg_byte);

    /* Increments address stored in HL */
    write_reg<Register16Bit::REG_HL>(addr - 1);
  }

  std::string describe() override { return IroGB::format("LDD HL, A"); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }
};

/*
 * Copy from address specified by HL into A, decrement HL
 */
class LDD_A_HL final : public Instruction {
public:
  LDD_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);

    /* Increments address stored in HL */
    write_reg<Register16Bit::REG_HL>(addr - 1);
  }

  std::string describe() override { return IroGB::format("LDD A, HL"); }
  std::size_t next_sync_cycle() override { return 4; }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }
};

/*
 * Read 16-bit immediate into 16-bit register XX
 */
class LD_XX_imm16 final : public Instruction {
public:
  LD_XX_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register16Bit src)
      : Instruction(reg_file_ptr, bus_ptr), src(src) {}

  void exec() override { write_reg(src, addr); }

  std::string describe() override {
    return IroGB::format("LD {}, {}", to_string(src), static_cast<int>(addr));
  }

  InstructionTiming parse() override {
    addr = bus->read_byte(reg_file->reg_pc++);
    addr |= static_cast<addr_t>(bus->read_byte(reg_file->reg_pc++)) << 8;

    return {
        .total_cycles = 12,
        .sync_events = 1,
    };
  }

private:
  const Register16Bit src;
  addr_t addr{};
};

/*
 * Stores SP to address to 16-bit immediate address
 */
class LD_imm16_SP final : public Instruction {
public:
  LD_imm16_SP(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    bus->write_byte(addr, reg_file->reg_sp.read_lo());
    bus->write_byte(addr + 1, reg_file->reg_sp.read_hi());
  }

  std::string describe() override { return IroGB::format("LD {}, sp", static_cast<int>(addr)); }

  InstructionTiming parse() override {
    addr = bus->read_byte(reg_file->reg_pc++);
    addr |= static_cast<addr_t>(bus->read_byte(reg_file->reg_pc++)) << 8;

    return {
        .total_cycles = 20,
        .sync_events = 1,
    };
  }

private:
  addr_t addr{};
};

/*
 * Copies HL register value into SP
 */
class LD_SP_HL final : public Instruction {
public:
  LD_SP_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr) : Instruction(reg_file_ptr, bus_ptr) {}

  void exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    reg_file->reg_sp.write(addr);
  }

  std::string describe() override { return IroGB::format("LD SP, HL"); }

  InstructionTiming parse() override {
    return {
        .total_cycles = 8,
        .sync_events = 1,
    };
  }
};

/*
 * Push 16-bit register value
 */
class PUSH_XX final : public Instruction {
public:
  PUSH_XX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register16Bit src)
      : Instruction(reg_file_ptr, bus_ptr), src(src) {}

  void exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(--sp, read_reg(src) >> 8);
      state = InstrStates::INSTR_STATE_WRITE2;
      break;
    case InstrStates::INSTR_STATE_WRITE2:
      bus->write_byte(--sp, read_reg(src) & 0xFF);
      reg_file->reg_sp.write(sp);
      break;
    default:
      break;
    }
  }

  std::size_t next_sync_cycle() override {
    return state == InstrStates::INSTR_STATE_WRITE ? 8 : 12;
  }
  std::string describe() override { return IroGB::format("PUSH {}", to_string(src)); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_WRITE;
    sp = reg_file->reg_sp.read();

    return {
        .total_cycles = 16,
        .sync_events = 2,
    };
  }

private:
  const Register16Bit src;
  InstrStates state{};
  addr_t sp{};
};

/*
 * Pop 16-bit register value
 */
class POP_XX final : public Instruction {
public:
  POP_XX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, Register16Bit dst)
      : Instruction(reg_file_ptr, bus_ptr), dst(dst) {}

  void exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      state = InstrStates::INSTR_STATE_READ2;
      addr = bus->read_byte(sp++);
      break;
    case InstrStates::INSTR_STATE_READ2:
      addr |= static_cast<addr_t>(bus->read_byte(sp++)) << 8;
      reg_file->reg_sp.write(sp);
      write_reg(dst, addr);
    default:
      break;
    }
  }

  std::size_t next_sync_cycle() override { return state == InstrStates::INSTR_STATE_READ ? 4 : 8; }
  std::string describe() override { return IroGB::format("POP {}", to_string(dst)); }

  InstructionTiming parse() override {
    state = InstrStates::INSTR_STATE_READ;
    sp = reg_file->reg_sp.read();

    return {
        .total_cycles = 12,
        .sync_events = 2,
    };
  }

private:
  const Register16Bit dst;
  InstrStates state{};
  addr_t sp{}, addr{};
};

#endif // GBC_MOVES_HPP
