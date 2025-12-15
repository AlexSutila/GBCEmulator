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
    const byte_t imm = bus->read_byte(reg_file->reg_pc++);
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
    const addr_t sum = static_cast<uint16_t>(a) + static_cast<uint16_t>(x) +
                       static_cast<uint16_t>(carry);
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
    const byte_t imm = bus->read_byte(reg_file->reg_pc++);
    const byte_t carry =
        reg_file->reg_af.get_flag(StatusFlagMask::FLAG_C_MASK) ? 1 : 0;

    // Compute sum including carry flag
    const addr_t sum = static_cast<uint16_t>(a) + static_cast<uint16_t>(imm) +
                       static_cast<uint16_t>(carry);
    const byte_t result = static_cast<byte_t>(sum);

    // Update flags
    const bool half_carry = ((a & 0x0F) + (imm & 0x0F) + carry) > 0x0F;
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_Z_MASK, result == 0);
    reg_file->reg_af.clr_flag(StatusFlagMask::FLAG_N_MASK);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_H_MASK, half_carry);
    reg_file->reg_af.put_flag(StatusFlagMask::FLAG_C_MASK, sum > 0xFF);
    return {8, 8};
  }
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
    const addr_t sum = static_cast<uint16_t>(a) + static_cast<uint16_t>(n) +
                       static_cast<uint16_t>(carry);
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

#endif // __ALU_H
