#ifndef GBC_ALU_HPP
#define GBC_ALU_HPP

#include "cpu/instr/instr.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "memory/bus.hpp"

#include <cstdint>
#include <format>

/*
 * Add value in 8-bit register X to A
 */
template <Register8Bit src> class ADD_A_X final : public Instruction {
public:
  ADD_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t result = a + x;

    // Update flags
    const bool c = static_cast<addr_t>(a) + static_cast<addr_t>(x) > 0xFF;
    const bool h = ((a & 0x0F) + (x & 0x0F)) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, h);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, c);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 4;
  }
  std::string describe() override {
    return std::format("ADD A, {}", to_string<src>());
  }
};

/*
 * Add immediate 8-bit value to A
 */
class ADD_A_imm8 final : public Instruction {
public:
  ADD_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a + imm;

    // Update flags
    const bool carry = static_cast<addr_t>(a) + static_cast<addr_t>(imm) > 0xFF;
    const bool half_carry = ((a & 0x0F) + (imm & 0x0F)) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 8;
  }
  std::string describe() override {
    return std::format("ADD A, {}", static_cast<int>(imm));
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm{};
};

/*
 * Add 8-bit value pointed to by HL to A
 */
class ADD_A_HL final : public Instruction {
public:
  ADD_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a + n;

    // Update flags
    const bool carry = static_cast<addr_t>(a) + static_cast<addr_t>(n) > 0xFF;
    const bool half_carry = ((a & 0x0F) + (n & 0x0F)) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 8;
  }
  std::string describe() override { return std::format("ADD A, HL"); }
  std::size_t mem_access_t_cycle() override { return 4; }
};

/*
 * Add value in 8-bit register X to A with carry
 */
template <Register8Bit src> class ADC_A_X final : public Instruction {
public:
  ADC_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute sum including carry flag
    const addr_t sum = static_cast<addr_t>(a) + static_cast<addr_t>(x) +
                       static_cast<addr_t>(carry);
    const auto result = static_cast<byte_t>(sum);

    // Update flags
    const bool half_carry = ((a & 0x0F) + (x & 0x0F) + carry) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, sum > 0xFF);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 4;
  }
  std::string describe() override {
    return std::format("ADC A, {}", to_string<src>());
  }
};

/*
 * Add immediate 8-bit value to A with carry
 */
class ADC_A_imm8 final : public Instruction {
public:
  ADC_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute sum including carry flag
    const addr_t sum = static_cast<addr_t>(a) + static_cast<addr_t>(imm) +
                       static_cast<addr_t>(carry);
    const auto result = static_cast<byte_t>(sum);

    // Update flags
    const bool half_carry = ((a & 0x0F) + (imm & 0x0F) + carry) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, sum > 0xFF);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 8;
  }
  std::string describe() override {
    return std::format("ADC A, {}", static_cast<int>(imm));
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm{};
};

/*
 * Add 8-bit value pointed to by HL to A with carry
 */
class ADC_A_HL final : public Instruction {
public:
  ADC_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute sum including carry flag
    const addr_t sum = static_cast<addr_t>(a) + static_cast<addr_t>(n) +
                       static_cast<addr_t>(carry);
    const auto result = static_cast<byte_t>(sum);

    // Update flags
    const bool half_carry = ((a & 0x0F) + (n & 0x0F) + carry) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, sum > 0xFF);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 8;
  }
  std::string describe() override { return std::format("ADC A, HL"); }
  std::size_t mem_access_t_cycle() override { return 4; }
};

/*
 * Subtract value in 8-bit register X from A
 */
template <Register8Bit src> class SUB_A_X final : public Instruction {
public:
  SUB_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 4;
  }
  std::string describe() override {
    return std::format("SUB A, {}", to_string<src>());
  }
};

/*
 * Subtract immediate 8-bit value from A
 */
class SUB_A_imm8 final : public Instruction {
public:
  SUB_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 8;
  }
  std::string describe() override {
    return std::format("SUB A, {}", static_cast<int>(imm));
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm{};
};

/*
 * Subtract 8-bit value pointed to by HL to A
 */
class SUB_A_HL final : public Instruction {
public:
  SUB_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 8;
  }
  std::string describe() override { return std::format("SUB A, HL"); }
  std::size_t mem_access_t_cycle() override { return 4; }
};

/*
 * Subtract value in 8-bit register X from A with carry
 */
template <Register8Bit src> class SBC_A_X final : public Instruction {
public:
  SBC_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute difference including carry flag
    const addr_t diff = static_cast<addr_t>(a) - static_cast<addr_t>(x) -
                        static_cast<addr_t>(carry);
    const auto result = static_cast<byte_t>(diff);

    // Update flags
    const bool c = static_cast<addr_t>(a) < static_cast<addr_t>(x + carry);
    const bool h = (a & 0x0F) < (x & 0x0F) + carry;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, h);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, c);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 4;
  }
  std::string describe() override {
    return std::format("SBC A, {}", to_string<src>());
  }
};

/*
 * Subtract 8-bit immediate value from A with carry
 */
class SBC_A_imm8 final : public Instruction {
public:
  SBC_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute difference including carry flag
    const addr_t diff = static_cast<addr_t>(a) - static_cast<addr_t>(imm) -
                        static_cast<addr_t>(carry);
    const auto result = static_cast<byte_t>(diff);

    // Update flags
    const bool c = static_cast<addr_t>(a) < static_cast<addr_t>(imm + carry);
    const bool h = (a & 0x0F) < (imm & 0x0F) + carry;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, h);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, c);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 8;
  }
  std::string describe() override {
    return std::format("SBC A, {}", static_cast<int>(imm));
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm{};
};

/*
 * Subtract 8-bit immediate value from A with carry
 */
class SBC_A_HL final : public Instruction {
public:
  SBC_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute difference including carry flag
    const addr_t diff = static_cast<addr_t>(a) - static_cast<addr_t>(n) -
                        static_cast<addr_t>(carry);
    const auto result = static_cast<byte_t>(diff);

    // Update flags
    const bool c = static_cast<addr_t>(a) < static_cast<addr_t>(n + carry);
    const bool h = (a & 0x0F) < (n & 0x0F) + carry;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, h);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, c);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 8;
  }
  std::string describe() override { return std::format("SBC A, HL"); }
  std::size_t mem_access_t_cycle() override { return 4; }
};

/*
 * Bitwise AND value from 8-bit register X with A
 */
template <Register8Bit src> class AND_A_X final : public Instruction {
public:
  AND_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 4;
  }
  std::string describe() override {
    return std::format("AND A, {}", to_string<src>());
  }
};

/*
 * Bitwise AND immediate 8-bit value X with A
 */
class AND_A_imm8 final : public Instruction {
public:
  AND_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a & imm;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 8;
  }
  std::string describe() override {
    return std::format("AND A, {}", static_cast<int>(imm));
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm{};
};

/*
 * Bitwise AND 8-bit value pointed to by HL with A
 */
class AND_A_HL final : public Instruction {
public:
  AND_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 8;
  }
  std::string describe() override { return std::format("AND A, HL"); }
  std::size_t mem_access_t_cycle() override { return 4; }
};

/*
 * Bitwise XOR value from 8-bit register X with A
 */
template <Register8Bit src> class XOR_A_X final : public Instruction {
public:
  XOR_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 4;
  }
  std::string describe() override {
    return std::format("XOR A, {}", to_string<src>());
  }
};

/*
 * Bitwise XOR immediate 8-bit value X with A
 */
class XOR_A_imm8 final : public Instruction {
public:
  XOR_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a ^ imm;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 8;
  }
  std::string describe() override {
    return std::format("XOR A, {}", static_cast<int>(imm));
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm{};
};

/*
 * Bitwise XOR 8-bit value pointed to by HL with A
 */
class XOR_A_HL final : public Instruction {
public:
  XOR_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 8;
  }
  std::string describe() override { return std::format("XOR A, HL"); }
  std::size_t mem_access_t_cycle() override { return 4; }
};

/*
 * Bitwise OR value from 8-bit register X with A
 */
template <Register8Bit src> class OR_A_X final : public Instruction {
public:
  OR_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 4;
  }
  std::string describe() override {
    return std::format("OR A, {}", to_string<src>());
  }
};

/*
 * Bitwise OR immediate 8-bit value X with A
 */
class OR_A_imm8 final : public Instruction {
public:
  OR_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t result = a | imm;

    // Update flags
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_H_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_C_MASK);

    // Write back
    write_reg<Register8Bit::REG_A>(result);
    return 8;
  }
  std::string describe() override {
    return std::format("OR A, {}", static_cast<int>(imm));
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm{};
};

/*
 * Bitwise OR 8-bit value pointed to by HL with A
 */
class OR_A_HL final : public Instruction {
public:
  OR_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 8;
  }
  std::string describe() override { return std::format("OR A, HL"); }
  std::size_t mem_access_t_cycle() override { return 4; }
};

/*
 * Compare 8-bit register X with A. This is basically a subtract operation,
 * but it throws away the result and only updates the flags.
 */
template <Register8Bit src> class CP_A_X final : public Instruction {
public:
  CP_A_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();
    const byte_t x = read_reg<src>();

    // Update flags
    const bool half_carry = (a & 0x0F) < (x & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, a == x);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < x);
    return 4;
  }
  std::string describe() override {
    return std::format("CP A, {}", to_string<src>());
  }
};

/*
 * Compare immediate 8-bit value with A. This is basically a subtract operation,
 * but it throws away the result and only updates the flags.
 */
class CP_A_imm8 final : public Instruction {
public:
  CP_A_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t a = read_reg<Register8Bit::REG_A>();

    // Update flags
    const bool half_carry = (a & 0x0F) < (imm & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, a == imm);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < imm);
    return 8;
  }
  std::string describe() override {
    return std::format("CP A, {}", static_cast<int>(imm));
  }
  void parse() override { imm = bus->read_byte(reg_file->reg_pc++); }

private:
  byte_t imm{};
};

/*
 * Compare 8-bit value pointed to by HL with A. This is basically a subtract
 * operation, but it throws away the result and only updates the flags.
 */
class CP_A_HL final : public Instruction {
public:
  CP_A_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
    const byte_t a = read_reg<Register8Bit::REG_A>();

    // Update flags
    const bool half_carry = (a & 0x0F) < (n & 0x0F);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, a == n);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, a < n);
    return 8;
  }
  std::string describe() override { return std::format("CP A, HL"); }
  std::size_t mem_access_t_cycle() override { return 4; }
};

/*
 * Increment contents of 8-bit register X
 */
template <Register8Bit src> class INC_X final : public Instruction {
public:
  INC_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<src>();
    const byte_t result = x + 1;

    // Update flags - C is left alone for this instruction
    const bool half_carry = (x & 0x0F) == 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);

    // Write back
    write_reg<src>(result);
    return 4;
  }
  std::string describe() override {
    return std::format("INC {}", to_string<src>());
  }
};

/*
 * Increment contents pointed to by register HL
 */
class INC_HL final : public Instruction {
public:
  INC_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
      result = n + 1;

      // Update flags - C is left alone for this instruction
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, (n & 0xF) == 0xF);
      state = InstrStates::INSTR_STATE_WRITE;
      break;
    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(read_reg<Register16Bit::REG_HL>(), result);
      break;
    default:
      break;
    }
    return 12;
  }
  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 4 : 8;
  }
  std::string describe() override { return std::format("INC HL"); }
  void parse() override { state = InstrStates::INSTR_STATE_READ; }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
};

/*
 * Decrement contents of 8-bit register X
 */
template <Register8Bit src> class DEC_X final : public Instruction {
public:
  DEC_X(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const byte_t x = read_reg<src>();
    const byte_t result = x - 1;

    // Update flags
    const bool half_carry = (x & 0x0F) == 0x00;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);

    // Write back
    write_reg<src>(result);
    return 4;
  }
  std::string describe() override {
    return std::format("DEC {}", to_string<src>());
  }
};

/*
 * Decrement contents pointed to by register HL
 */
class DEC_HL final : public Instruction {
public:
  DEC_HL(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    switch (state) {
    case InstrStates::INSTR_STATE_READ:
      n = bus->read_byte(read_reg<Register16Bit::REG_HL>());
      result = n - 1;

      // Update flags
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
      reg_file->reg_af.set_flag(StatusFlagMask::FLAG_N_MASK);
      reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, (n & 0xF) == 0);
      state = InstrStates::INSTR_STATE_WRITE;
      break;
    case InstrStates::INSTR_STATE_WRITE:
      bus->write_byte(read_reg<Register16Bit::REG_HL>(), result);
      break;
    default:
      break;
    }
    return 12;
  }
  std::size_t mem_access_t_cycle() override {
    return state == InstrStates::INSTR_STATE_READ ? 4 : 8;
  }
  std::string describe() override { return std::format("DEC HL"); }
  void parse() override { state = InstrStates::INSTR_STATE_READ; }

private:
  InstrStates state{};
  byte_t result{};
  byte_t n{};
};

/*
 * I will never understand what this shit does ngl lol. Decimal adjust?
 */
class DAA final : public Instruction {
public:
  DAA(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
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
    return 4;
  }
  std::string describe() override { return std::format("DAA"); }
};

/*
 * Increment HL register by contents of 16-bit XX register
 */
template <Register16Bit src> class ADD_HL_XX final : public Instruction {
public:
  ADD_HL_XX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t hl = read_reg<Register16Bit::REG_HL>();
    const addr_t xx = read_reg<src>();
    const std::uint32_t sum = static_cast<std::uint32_t>(hl) + xx;

    // Update flags
    const bool half_carry = ((hl & 0x0FFF) + (xx & 0x0FFF)) > 0x0FFF;
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, sum > 0xFFFF);

    // Write back
    write_reg<Register16Bit::REG_HL>(static_cast<addr_t>(sum));
    return 8;
  }
  std::string describe() override {
    return std::format("ADD HL, {}", to_string<src>());
  }
};

/*
 * Increment contents of 16-bit XX register
 */
template <Register16Bit dst> class INC_XX final : public Instruction {
public:
  INC_XX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t xx = read_reg<dst>();
    write_reg<dst>(xx + 1);
    return 8;
  }
  std::string describe() override {
    return std::format("INC {}", to_string<dst>());
  }
};

/*
 * Decrement contents of 16-bit XX register
 */
template <Register16Bit dst> class DEC_XX final : public Instruction {
public:
  DEC_XX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t xx = read_reg<dst>();
    write_reg<dst>(xx - 1);
    return 8;
  }
  std::string describe() override {
    return std::format("DEC {}", to_string<dst>());
  }
};

/*
 * Add 8-bit immediate value to stack pointer - NOTE: imm is signed
 */
class ADD_SP_imm8 final : public Instruction {
public:
  ADD_SP_imm8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t nn = static_cast<addr_t>(imm);
    const addr_t sp = reg_file->reg_sp.read();

    // Update flags
    const bool half_carry = ((sp & 0x0F) + (nn & 0x0F)) > 0x0F;
    const bool carry = ((sp & 0xFF) + (nn & 0xFF)) > 0xFF;
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_Z_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    reg_file->reg_sp.write(sp + nn);
    return 16;
  }
  std::string describe() override {
    return std::format("ADD SP, {}", static_cast<int>(imm));
  }
  void parse() override {
    imm = static_cast<int8_t>(bus->read_byte(reg_file->reg_pc++));
  }

private:
  std::int8_t imm{}; // Signed intentionally
};

/*
 * Add 8-bit immediate value to stack pointer, store result in HL
 * - NOTE: imm is signed
 */
class LD_HL_SP_E8 final : public Instruction {
public:
  LD_HL_SP_E8(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::size_t exec() override {
    const addr_t nn = static_cast<addr_t>(imm);
    const addr_t sp = reg_file->reg_sp.read();

    // Update flags
    const bool half_carry = ((sp & 0x0F) + (nn & 0x0F)) > 0x0F;
    const bool carry = ((sp & 0xFF) + (nn & 0xFF)) > 0xFF;
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_Z_MASK);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, carry);

    // Write back
    write_reg<Register16Bit::REG_HL>(sp + nn);
    return 12;
  }
  std::string describe() override {
    return std::format("LD HL, SP+{}", static_cast<int>(imm));
  }
  void parse() override {
    imm = static_cast<int8_t>(bus->read_byte(reg_file->reg_pc++));
  }

private:
  std::int8_t imm{}; // Signed intentionally
};

#endif // GBC_ALU_HPP
