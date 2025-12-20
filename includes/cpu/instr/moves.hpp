#ifndef __MOVES_H
#define __MOVES_H

#include "cpu/instr/instr.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "memory/bus.hpp"

/*
 * Copies value from Y into X, operates only on 8-bit registers
 */
template <Register8Bit dst, Register8Bit src>
class LD_X_Y : public Instruction {
public:
  LD_X_Y(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t val = read_reg<src>();
    write_reg<dst>(val);
    return 4;
  }
};

/*
 * Copies immediate value into X, operates only on 8-bit registers
 */
template <Register8Bit dst> class LD_X_imm8 : public Instruction {
public:
  LD_X_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    write_reg<dst>(imm);
    return 8;
  }
  void parse() override {
    const addr_t addr = reg_file->reg_pc++;
    imm = bus->read_byte(addr);
  }

private:
  byte_t imm;
};

/*
 * Copies byte read from address HL into X, operates only on 8-bit registers
 */
template <Register8Bit dst> class LD_X_HL : public Instruction {
public:
  LD_X_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t mem_byte = bus->read_byte(addr);
    write_reg<dst>(mem_byte);
    return 8;
  }
};

/*
 * Copies byte from X into address HL, operates only on 8-bit registers
 */
template <Register8Bit src> class LD_HL_X : public Instruction {
public:
  LD_HL_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t reg_byte = read_reg<src>();
    bus->write_byte(addr, reg_byte);
    return 8;
  }
};

/*
 * Copies 8-bit immedaite value into address HL
 */
class LD_HL_imm8 : public Instruction {
public:
  LD_HL_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    bus->write_byte(addr, imm);
    return 12;
  }
  void parse() override {
    const addr_t addr = reg_file->reg_pc++;
    imm = bus->read_byte(addr);
  }

private:
  byte_t imm;
};

/*
 * Copies 8-bit value from address specified by register XX into A
 */
template <Register16Bit src> class LD_A_XX : public Instruction {
public:
  LD_A_XX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const addr_t addr = read_reg<src>();
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);
    return 8;
  }
};

/*
 * Copies 8-bit value from immediate address value into A
 */
class LD_A_imm16 : public Instruction {
public:
  LD_A_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);
    return 16;
  }
  void parse() override {
    const byte_t lsb = bus->read_byte(reg_file->reg_pc++);
    const byte_t msb = bus->read_byte(reg_file->reg_pc++);
    addr = (msb << 8) | lsb;
  }

private:
  addr_t addr;
};

/*
 * Copies A into address specified by 16-bit register XX
 */
template <Register16Bit dst> class LD_XX_A : public Instruction {
public:
  LD_XX_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    const addr_t addr = read_reg<dst>();
    bus->write_byte(addr, reg_byte);
    return 8;
  }
};

/*
 * Copies A into address specified by 16-bit immediate value
 */
class LD_imm16_A : public Instruction {
public:
  LD_imm16_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    bus->write_byte(addr, reg_byte);
    return 16;
  }
  void parse() override {
    const byte_t lsb = bus->read_byte(reg_file->reg_pc++);
    const byte_t msb = bus->read_byte(reg_file->reg_pc++);
    addr = (msb << 8) | lsb;
  }

private:
  addr_t addr;
};

/*
 * Copy IO-register specified by 8-bit immediate into A
 */
class LDH_A_imm8 : public Instruction {
public:
  LDH_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);
    return 12;
  }
  void parse() override { addr = 0xFF00 | bus->read_byte(reg_file->reg_pc++); }

private:
  addr_t addr;
};

/*
 * Copy A into IO-register specified by 8-bit immediate
 */
class LDH_imm8_A : public Instruction {
public:
  LDH_imm8_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    bus->write_byte(addr, reg_byte);
    return 12;
  }
  void parse() override { addr = 0xFF00 | bus->read_byte(reg_file->reg_pc++); }

private:
  addr_t addr;
};

/*
 * Copy A into IO-register specified by register C
 */
class LDH_C_A : public Instruction {
public:
  LDH_C_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    bus->write_byte(0xFF00 | read_reg<Register8Bit::REG_C>(), reg_byte);
    return 8;
  }
};

/*
 * Copy IO-register specified by register C into A
 */
class LDH_A_C : public Instruction {
public:
  LDH_A_C(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t mem_byte =
        bus->read_byte(0xFF00 | read_reg<Register8Bit::REG_C>());
    reg_file->reg_af.write_hi(mem_byte);
    return 8;
  }
};

/*
 * Copy A into address specified by HL, increment HL
 */
class LDI_HL_A : public Instruction {
public:
  LDI_HL_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    bus->write_byte(addr, reg_byte);

    /* Increments address stored in HL */
    write_reg<Register16Bit::REG_HL>(addr + 1);
    return 8;
  }
};

/*
 * Copy from address specified by HL into A, increment HL
 */
class LDI_A_HL : public Instruction {
public:
  LDI_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);

    /* Increments address stored in HL */
    write_reg<Register16Bit::REG_HL>(addr + 1);
    return 8;
  }
};

/*
 * Copy A into address specified by HL, decrement HL
 */
class LDD_HL_A : public Instruction {
public:
  LDD_HL_A(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const byte_t reg_byte = read_reg<Register8Bit::REG_A>();
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    bus->write_byte(addr, reg_byte);

    /* Increments address stored in HL */
    write_reg<Register16Bit::REG_HL>(addr - 1);
    return 8;
  }
};

/*
 * Copy from address specified by HL into A, decrement HL
 */
class LDD_A_HL : public Instruction {
public:
  LDD_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t mem_byte = bus->read_byte(addr);
    reg_file->reg_af.write_hi(mem_byte);

    /* Increments address stored in HL */
    write_reg<Register16Bit::REG_HL>(addr - 1);
    return 8;
  }
};

/*
 * Read 16-bit immediate into 16-bit register XX
 */
template <Register16Bit src> class LD_XX_imm16 : public Instruction {
public:
  LD_XX_imm16(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    write_reg<src>(addr);
    return 12;
  }
  void parse() override {
    addr = bus->read_byte(reg_file->reg_pc++);
    addr |= (addr_t)bus->read_byte(reg_file->reg_pc++) << 8;
  }

private:
  addr_t addr;
};

/*
 * Stores SP to address to 16-bit immediate address
 */
class LD_imm16_SP : public Instruction {
public:
  LD_imm16_SP(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    bus->write_byte(addr, reg_file->reg_sp.read_lo());
    bus->write_byte(addr + 1, reg_file->reg_sp.read_hi());
    return 20;
  }
  void parse() override {
    addr = bus->read_byte(reg_file->reg_pc++);
    addr |= (addr_t)bus->read_byte(reg_file->reg_pc++) << 8;
  }

private:
  addr_t addr;
};

/*
 * Copies HL register value into SP
 */
class LD_SP_HL : public Instruction {
public:
  LD_SP_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    reg_file->reg_sp.write(addr);
    return 8;
  }
};

/*
 * Push 16-bit register value
 */
template <Register16Bit src> class PUSH_XX : public Instruction {
public:
  PUSH_XX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    addr_t sp = reg_file->reg_sp.read();
    const addr_t addr = read_reg<src>();
    bus->write_byte(--sp, addr >> 8);
    bus->write_byte(--sp, addr & 0xFF);

    // Write back
    reg_file->reg_sp.write(sp);
    return 16;
  }
};

/*
 * Pop 16-bit registe value
 */
template <Register16Bit dst> class POP_XX : public Instruction {
public:
  POP_XX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t step() override {
    addr_t sp = reg_file->reg_sp.read();
    addr_t addr = bus->read_byte(sp++);
    addr |= (addr_t)bus->read_byte(sp++) << 8;

    // Write back
    reg_file->reg_sp.write(sp);
    write_reg<dst>(addr);
    return 12;
  }
};

#endif // __MOVES_H
