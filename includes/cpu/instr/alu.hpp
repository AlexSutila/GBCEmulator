#ifndef __ALU_H
#define __ALU_H

#include "cpu/registers/flags.hpp"
#include <cpu/instr/instr.hpp>
#include <cpu/registers/regfile.hpp>
#include <emu_types.hpp>
#include <memory/bus.hpp>

/*
 * Add value in 8-bit register X to A
 */
template <Register8Bit src> class ADD_A_X : public Instruction {
public:
  ADD_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t result = a + x;

    // Update flags
    const bool c = static_cast<addr_t>(a) + static_cast<addr_t>(x) > 0xFF;
    const bool h = ((a & 0x0F) + (x & 0x0F)) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, h);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, c);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

/*
 * Add immediate 8-bit value to A
 */
class ADD_A_imm8 : public Instruction {
public:
  ADD_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a + imm;

    // Update flags
    const bool carry = static_cast<addr_t>(a) + static_cast<addr_t>(imm) > 0xFF;
    const bool half_carry = ((a & 0x0F) + (imm & 0x0F)) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, carry);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm;
};

/*
 * Add 8-bit value pointed to by HL to A
 */
class ADD_A_HL : public Instruction {
public:
  ADD_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a + n;

    // Update flags
    const bool carry = static_cast<addr_t>(a) + static_cast<addr_t>(n) > 0xFF;
    const bool half_carry = ((a & 0x0F) + (n & 0x0F)) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, carry);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
};

/*
 * Add value in 8-bit register X to A with carry
 */
template <Register8Bit src> class ADC_A_X : public Instruction {
public:
  ADC_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute sum including carry flag
    const addr_t sum = static_cast<addr_t>(a) + static_cast<addr_t>(x) +
                       static_cast<addr_t>(carry);
    const byte_t result = static_cast<byte_t>(sum);

    // Update flags
    const bool half_carry = ((a & 0x0F) + (x & 0x0F) + carry) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, sum > 0xFF);
    return {4, 4};
  }
};

/*
 * Add immediate 8-bit value to A with carry
 */
class ADC_A_imm8 : public Instruction {
public:
  ADC_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute sum including carry flag
    const addr_t sum = static_cast<addr_t>(a) + static_cast<addr_t>(imm) +
                       static_cast<addr_t>(carry);
    const byte_t result = static_cast<byte_t>(sum);

    // Update flags
    const bool half_carry = ((a & 0x0F) + (imm & 0x0F) + carry) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, sum > 0xFF);
    return {8, 8};
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm;
};

/*
 * Add 8-bit value pointed to by HL to A with carry
 */
class ADC_A_HL : public Instruction {
public:
  ADC_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute sum including carry flag
    const addr_t sum = static_cast<addr_t>(a) + static_cast<addr_t>(n) +
                       static_cast<addr_t>(carry);
    const byte_t result = static_cast<byte_t>(sum);

    // Update flags
    const bool half_carry = ((a & 0x0F) + (n & 0x0F) + carry) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, sum > 0xFF);
    return {8, 8};
  }
};

/*
 * Subtract value in 8-bit register X from A
 */
template <Register8Bit src> class SUB_A_X : public Instruction {
public:
  SUB_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t result = a - x;

    // Update flags
    const bool half_carry = (a & 0x0F) < (x & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < x);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

/*
 * Subtract immediate 8-bit value from A
 */
class SUB_A_imm8 : public Instruction {
public:
  SUB_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a - imm;

    // Update flags
    const bool half_carry = (a & 0x0F) < (imm & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < imm);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm;
};

/*
 * Subtract 8-bit value pointed to by HL to A
 */
class SUB_A_HL : public Instruction {
public:
  SUB_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a - n;

    // Update flags
    const bool half_carry = (a & 0x0F) < (n & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < n);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
};

/*
 * Subtract value in 8-bit register X from A with carry
 */
template <Register8Bit src> class SBC_A_X : public Instruction {
public:
  SBC_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute difference including carry flag
    const addr_t diff = static_cast<addr_t>(a) - static_cast<addr_t>(x) -
                        static_cast<addr_t>(carry);
    const byte_t result = static_cast<byte_t>(diff);

    // Update flags
    const bool half_carry = (a & 0x0F) < (x & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < x);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

/*
 * Subtract 8-bit immediate value from A with carry
 */
class SBC_A_imm8 : public Instruction {
public:
  SBC_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute difference including carry flag
    const addr_t diff = static_cast<addr_t>(a) - static_cast<addr_t>(imm) -
                        static_cast<addr_t>(carry);
    const byte_t result = static_cast<byte_t>(diff);

    // Update flags
    const bool half_carry = (a & 0x0F) < (imm & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < imm);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm;
};

/*
 * Subtract 8-bit immediate value from A with carry
 */
class SBC_A_HL : public Instruction {
public:
  SBC_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute difference including carry flag
    const addr_t diff = static_cast<addr_t>(a) - static_cast<addr_t>(n) -
                        static_cast<addr_t>(carry);
    const byte_t result = static_cast<byte_t>(diff);

    // Update flags
    const bool half_carry = (a & 0x0F) < (n & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < n);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
};

/*
 * Bitwise AND value from 8-bit register X with A
 */
template <Register8Bit src> class AND_A_X : public Instruction {
public:
  AND_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t result = a & x;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

/*
 * Bitwise AND immediate 8-bit value X with A
 */
class AND_A_imm8 : public Instruction {
public:
  AND_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a & imm;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm;
};

/*
 * Bitwise AND 8-bit value pointed to by HL with A
 */
class AND_A_HL : public Instruction {
public:
  AND_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a & n;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
};

/*
 * Bitwise XOR value from 8-bit register X with A
 */
template <Register8Bit src> class XOR_A_X : public Instruction {
public:
  XOR_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t result = a ^ x;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

/*
 * Bitwise XOR immediate 8-bit value X with A
 */
class XOR_A_imm8 : public Instruction {
public:
  XOR_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a ^ imm;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm;
};

/*
 * Bitwise XOR 8-bit value pointed to by HL with A
 */
class XOR_A_HL : public Instruction {
public:
  XOR_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a ^ n;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
};

/*
 * Bitwise OR value from 8-bit register X with A
 */
template <Register8Bit src> class OR_A_X : public Instruction {
public:
  OR_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t result = a | x;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {4, 4};
  }
};

/*
 * Bitwise OR immediate 8-bit value X with A
 */
class OR_A_imm8 : public Instruction {
public:
  OR_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a | imm;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm;
};

/*
 * Bitwise OR 8-bit value pointed to by HL with A
 */
class OR_A_HL : public Instruction {
public:
  OR_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a | n;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return {8, 8};
  }
};

/*
 * Compare 8-bit register X with A. This is basically a subtract operation,
 * but it throws away the result and only updates the flags.
 */
template <Register8Bit src> class CP_A_X : public Instruction {
public:
  CP_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();

    // Update flags
    const bool half_carry = (a & 0x0F) < (x & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, a == x);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < x);
    return {4, 4};
  }
};

/*
 * Compare immediate 8-bit value with A. This is basically a subtract operation,
 * but it throws away the result and only updates the flags.
 */
class CP_A_imm8 : public Instruction {
public:
  CP_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();

    // Update flags
    const bool half_carry = (a & 0x0F) < (imm & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, a == imm);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < imm);
    return {8, 8};
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm;
};

/*
 * Compare 8-bit value pointed to by HL with A. This is basically a subtract
 * operation, but it throws away the result and only updates the flags.
 */
class CP_A_HL : public Instruction {
public:
  CP_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t a = read_reg<Register8Bit::REG_A>();

    // Update flags
    const bool half_carry = (a & 0x0F) < (n & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, a == n);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < n);
    return {8, 8};
  }
};

/*
 * Increment contents of 8-bit register X
 */
template <Register8Bit src> class INC_X : public Instruction {
public:
  INC_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t x = read_reg<src>();
    const byte_t result = x + 1;

    // Update flags - C is left alone for this instruction
    const bool half_carry = (x & 0x0F) == 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);

    // Write back
    write_reg<src>(result);
    return {4, 4};
  }
};

/*
 * Increment contents pointed to by register HL
 */
class INC_HL : public Instruction {
public:
  INC_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t result = n + 1;

    // Update flags - C is left alone for this instruction
    const bool half_carry = (n & 0x0F) == 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);

    // Write back
    bus->write_byte(read_reg<Register16Bit::REG_HL>(), result);
    return {12, 8};
  }
};

/*
 * Decrement contents of 8-bit register X
 */
template <Register8Bit src> class DEC_X : public Instruction {
public:
  DEC_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t x = read_reg<src>();
    const byte_t result = x - 1;

    // Update flags
    const bool half_carry = (x & 0x0F) == 0x00;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);

    // Write back
    write_reg<src>(result);
    return {4, 4};
  }
};

/*
 * Decrement contents pointed to by register HL
 */
class DEC_HL : public Instruction {
public:
  DEC_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t result = n - 1;

    // Update flags
    const bool half_carry = (n & 0x0F) == 0x00;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);

    // Write back
    bus->write_byte(read_reg<Register16Bit::REG_HL>(), result);
    return {12, 8};
  }
};

/*
 * I will never understand what this shit does ngl lol. Decimal adjust?
 */
class DAA : public Instruction {
public:
  DAA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<std::size_t, std::size_t> step() override {
    byte_t a = read_reg<Register8Bit::REG_A>();

    const bool n = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_N_MASK);
    const bool c = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK);
    const bool h = reg_file->reg_af.get_flag(StatusFlagMask::FLAG_H_MASK);

    // Post ADD/ADC instruction
    if (!n) {
      if (c || a > 0x99) {
        a += 0x60;
        reg_file->reg_af.set_flag(StatusFlagMask::FLAG_C_MASK);
      }
      if (h || (a & 0x0F) > 0x09) {
        a += 0x06;
      }
    }

    // Post SUB/SBC instruction
    else {
      if (c)
        a -= 0x60;
      if (h)
        a -= 0x06;
    }

    // Update flags - N is untouched and C is already handled above
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, a == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(a);
    return {4, 4};
  }
};

#endif // __ALU_H
