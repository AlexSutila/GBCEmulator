#ifndef __BITOPS_H
#define __BITOPS_H

#include "cpu/instr/instr.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "memory/bus.hpp"

#include <array>
#include <memory>

/*
 * Rotate left with carry
 */
class RLCA : public Instruction {
public:
  RLCA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
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
};

/*
 * Rotate right with carry
 */
class RRCA : public Instruction {
public:
  RRCA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
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
};

/*
 * Rotate left through carry
 */
class RLA : public Instruction {
public:
  RLA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
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
};

/*
 * Rotate right through carry
 */
class RRA : public Instruction {
public:
  RRA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
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
};

/*
 * The CB-prefix ISA extension. All instructions below this one lie under this
 * instruction set architecture extention.
 */
class CB_PREFIX : public Instruction {
public:
  CB_PREFIX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr);
  std::size_t exec() override;
  void parse() override;

private:
  using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
  byte_t op{};

  /* This is identical in behavior to the lookup table in lr35902.hpp */
  void init_cb_prefix(lookup_table_t &lookup);
  lookup_table_t lookup{};
};

template <Register8Bit dst> class RLC_X : public Instruction {
public:
  RLC_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<dst>();

    const bool carry = (x & 0x80) != 0x00;
    byte_t result = (x << 1) | (carry ? 0x01 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg<dst>(result);
    return 8;
  }
};

class RLC_HL : public Instruction {
public:
  RLC_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t n = bus->read_byte(addr);
    const bool carry = (n & 0x80) != 0x00;
    byte_t result = (n << 1) | (carry ? 0x01 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    bus->write_byte(addr, result);
    return 16;
  }
};

template <Register8Bit dst> class RL_X : public Instruction {
public:
  RL_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<dst>();
    const bool old_c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    const bool new_c = (x & 0x80) != 0x00;
    const byte_t result = (x << 1) | (old_c ? 0x01 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, new_c);

    // Write back
    write_reg<dst>(result);
    return 8;
  }
};

class RL_HL : public Instruction {
public:
  RL_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t hl = read_reg<Register16Bit::REG_HL>();
    const byte_t n = bus->read_byte(hl);
    const bool old_c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    const bool new_c = (n & 0x80) != 0x00;
    const byte_t result = (n << 1) | (old_c ? 0x01 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, new_c);

    // Write back
    bus->write_byte(hl, result);
    return 16;
  }
};

template <Register8Bit dst> class RRC_X : public Instruction {
public:
  RRC_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<dst>();
    const bool carry = (x & 0x01) != 0x00;
    const byte_t result = (x >> 1) | (carry ? 0x80 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg<dst>(result);
    return 8;
  }
};

class RRC_HL : public Instruction {
public:
  RRC_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t n = bus->read_byte(addr);

    const bool carry = (n & 0x01) != 0x00;
    const byte_t result = (n >> 1) | (carry ? 0x80 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    bus->write_byte(addr, result);
    return 16;
  }
};

template <Register8Bit dst> class RR_X : public Instruction {
public:
  RR_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<dst>();
    const bool old_c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    const bool new_c = (x & 0x01) != 0x00;
    const byte_t result = (x >> 1) | (old_c ? 0x80 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, new_c);

    // Write back
    write_reg<dst>(result);
    return 8;
  }
};

class RR_HL : public Instruction {
public:
  RR_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t n = bus->read_byte(addr);
    const bool old_c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    const bool new_c = (n & 0x01) != 0x00;
    const byte_t result = (n >> 1) | (old_c ? 0x80 : 0x00);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, new_c);

    // Write back
    bus->write_byte(addr, result);
    return 16;
  }
};

template <Register8Bit dst> class SLA_X : public Instruction {
public:
  SLA_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<dst>();
    const bool carry = (x & 0x80) != 0x00;
    const byte_t result = x << 1;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg<dst>(result);
    return 8;
  }
};

class SLA_HL : public Instruction {
public:
  SLA_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t n = bus->read_byte(addr);
    const bool carry = (n & 0x80) != 0x00;
    const byte_t result = n << 1;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    bus->write_byte(addr, result);
    return 16;
  }
};

template <Register8Bit dst> class SRA_X : public Instruction {
public:
  SRA_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<dst>();
    const bool carry = (x & 0x01) != 0x00;
    const byte_t result = (x & 0x80) | (x >> 1);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg<dst>(result);
    return 8;
  }
};

class SRA_HL : public Instruction {
public:
  SRA_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t n = bus->read_byte(addr);
    const bool carry = (n & 0x01) != 0x00;
    const byte_t result = (n & 0x80) | (n >> 1);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    bus->write_byte(addr, result);
    return 16;
  }
};

template <Register8Bit dst> class SWAP_X : public Instruction {
public:
  SWAP_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<dst>();
    const byte_t result = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<dst>(result);
    return 8;
  }
};

class SWAP_HL : public Instruction {
public:
  SWAP_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t n = bus->read_byte(addr);
    const byte_t result = ((n & 0xF0) >> 4) | ((n & 0x0F) << 4);

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    bus->write_byte(addr, result);
    return 16;
  }
};

template <Register8Bit dst> class SRL_X : public Instruction {
public:
  SRL_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<dst>();
    const bool carry = (x & 0x01) != 0x00;
    const byte_t result = x >> 1;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg<dst>(result);
    return 8;
  }
};

class SRL_HL : public Instruction {
public:
  SRL_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t addr = read_reg<Register16Bit::REG_HL>();
    const byte_t n = bus->read_byte(addr);
    const bool carry = (n & 0x01) != 0x00;
    const byte_t result = n >> 1;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    bus->write_byte(addr, result);
    return 16;
  }
};

template <byte_t bit, Register8Bit dst>
class BIT_N_X : public Instruction {
public:
  BIT_N_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<dst>();
    const bool bit_is_zero = ((x >> bit) & 0x01) == 0;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, bit_is_zero);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, true);
    return 8;
  }
};

template <byte_t bit>
class BIT_N_HL : public Instruction {
public:
  BIT_N_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
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
};

template <byte_t bit, Register8Bit dst>
class RES_N_X : public Instruction {
public:
  RES_N_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    byte_t x = read_reg<dst>();
    x &= ~(1 << bit);
    write_reg<dst>(x);
    return 8;
  }
};

template <byte_t bit>
class RES_N_HL : public Instruction {
public:
  RES_N_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t hl = read_reg<Register16Bit::REG_HL>();
    byte_t n = bus->read_byte(hl);
    n &= ~(1 << bit);
    bus->write_byte(hl, n);
    return 16;
  }
};

template <byte_t bit, Register8Bit dst>
class SET_N_X : public Instruction {
public:
  SET_N_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    byte_t x = read_reg<dst>();
    x |= (1 << bit);
    write_reg<dst>(x);
    return 8;
  }
};

template <byte_t bit>
class SET_N_HL : public Instruction {
public:
  SET_N_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t hl = read_reg<Register16Bit::REG_HL>();
    byte_t n = bus->read_byte(hl);
    n |= (1 << bit);
    bus->write_byte(hl, n);
    return 16;
  }
};

#endif // __BITOPS_H
