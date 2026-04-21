#include "cpu/instr/moves.hpp"
#include "cpu/lr35902.hpp"

#include <array>
#include <memory>

using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
using r16 = Register16Bit;
using r8 = Register8Bit;
using std::make_unique;

void LR35902::init_moves(lookup_table_t &lookup_) {
  lookup_.at(0x01) = make_unique<LD_XX_imm16>(&reg_file, bus, r16::REG_BC);
  lookup_.at(0x02) = make_unique<LD_XX_A>(&reg_file, bus, r16::REG_BC);
  lookup_.at(0x06) = make_unique<LD_X_imm8>(&reg_file, bus, r8::REG_B);
  lookup_.at(0x08) = make_unique<LD_imm16_SP>(&reg_file, bus);
  lookup_.at(0x0A) = make_unique<LD_A_XX>(&reg_file, bus, r16::REG_BC);
  lookup_.at(0x0E) = make_unique<LD_X_imm8>(&reg_file, bus, r8::REG_C);

  lookup_.at(0x11) = make_unique<LD_XX_imm16>(&reg_file, bus, r16::REG_DE);
  lookup_.at(0x12) = make_unique<LD_XX_A>(&reg_file, bus, r16::REG_DE);
  lookup_.at(0x16) = make_unique<LD_X_imm8>(&reg_file, bus, r8::REG_D);
  lookup_.at(0x1A) = make_unique<LD_A_XX>(&reg_file, bus, r16::REG_DE);
  lookup_.at(0x1E) = make_unique<LD_X_imm8>(&reg_file, bus, r8::REG_E);

  lookup_.at(0x21) = make_unique<LD_XX_imm16>(&reg_file, bus, r16::REG_HL);
  lookup_.at(0x22) = make_unique<LDI_HL_A>(&reg_file, bus);
  lookup_.at(0x26) = make_unique<LD_X_imm8>(&reg_file, bus, r8::REG_H);
  lookup_.at(0x2A) = make_unique<LDI_A_HL>(&reg_file, bus);
  lookup_.at(0x2E) = make_unique<LD_X_imm8>(&reg_file, bus, r8::REG_L);

  lookup_.at(0x31) = make_unique<LD_XX_imm16>(&reg_file, bus, r16::REG_SP);
  lookup_.at(0x32) = make_unique<LDD_HL_A>(&reg_file, bus);
  lookup_.at(0x36) = make_unique<LD_HL_imm8>(&reg_file, bus);
  lookup_.at(0x3A) = make_unique<LDD_A_HL>(&reg_file, bus);
  lookup_.at(0x3E) = make_unique<LD_X_imm8>(&reg_file, bus, r8::REG_A);

  lookup_.at(0x40) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_B, r8::REG_B);
  lookup_.at(0x41) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_B, r8::REG_C);
  lookup_.at(0x42) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_B, r8::REG_D);
  lookup_.at(0x43) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_B, r8::REG_E);
  lookup_.at(0x44) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_B, r8::REG_H);
  lookup_.at(0x45) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_B, r8::REG_L);
  lookup_.at(0x46) = make_unique<LD_X_HL>(&reg_file, bus, r8::REG_B);
  lookup_.at(0x47) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_B, r8::REG_A);
  lookup_.at(0x48) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_C, r8::REG_B);
  lookup_.at(0x49) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_C, r8::REG_C);
  lookup_.at(0x4A) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_C, r8::REG_D);
  lookup_.at(0x4B) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_C, r8::REG_E);
  lookup_.at(0x4C) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_C, r8::REG_H);
  lookup_.at(0x4D) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_C, r8::REG_L);
  lookup_.at(0x4E) = make_unique<LD_X_HL>(&reg_file, bus, r8::REG_C);
  lookup_.at(0x4F) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_C, r8::REG_A);

  lookup_.at(0x50) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_D, r8::REG_B);
  lookup_.at(0x51) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_D, r8::REG_C);
  lookup_.at(0x52) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_D, r8::REG_D);
  lookup_.at(0x53) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_D, r8::REG_E);
  lookup_.at(0x54) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_D, r8::REG_H);
  lookup_.at(0x55) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_D, r8::REG_L);
  lookup_.at(0x56) = make_unique<LD_X_HL>(&reg_file, bus, r8::REG_D);
  lookup_.at(0x57) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_D, r8::REG_A);
  lookup_.at(0x58) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_E, r8::REG_B);
  lookup_.at(0x59) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_E, r8::REG_C);
  lookup_.at(0x5A) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_E, r8::REG_D);
  lookup_.at(0x5B) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_E, r8::REG_E);
  lookup_.at(0x5C) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_E, r8::REG_H);
  lookup_.at(0x5D) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_E, r8::REG_L);
  lookup_.at(0x5E) = make_unique<LD_X_HL>(&reg_file, bus, r8::REG_E);
  lookup_.at(0x5F) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_E, r8::REG_A);

  lookup_.at(0x60) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_H, r8::REG_B);
  lookup_.at(0x61) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_H, r8::REG_C);
  lookup_.at(0x62) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_H, r8::REG_D);
  lookup_.at(0x63) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_H, r8::REG_E);
  lookup_.at(0x64) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_H, r8::REG_H);
  lookup_.at(0x65) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_H, r8::REG_L);
  lookup_.at(0x66) = make_unique<LD_X_HL>(&reg_file, bus, r8::REG_H);
  lookup_.at(0x67) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_H, r8::REG_A);
  lookup_.at(0x68) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_L, r8::REG_B);
  lookup_.at(0x69) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_L, r8::REG_C);
  lookup_.at(0x6A) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_L, r8::REG_D);
  lookup_.at(0x6B) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_L, r8::REG_E);
  lookup_.at(0x6C) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_L, r8::REG_H);
  lookup_.at(0x6D) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_L, r8::REG_L);
  lookup_.at(0x6E) = make_unique<LD_X_HL>(&reg_file, bus, r8::REG_L);
  lookup_.at(0x6F) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_L, r8::REG_A);

  lookup_.at(0x70) = make_unique<LD_HL_X>(&reg_file, bus, r8::REG_B);
  lookup_.at(0x71) = make_unique<LD_HL_X>(&reg_file, bus, r8::REG_C);
  lookup_.at(0x72) = make_unique<LD_HL_X>(&reg_file, bus, r8::REG_D);
  lookup_.at(0x73) = make_unique<LD_HL_X>(&reg_file, bus, r8::REG_E);
  lookup_.at(0x74) = make_unique<LD_HL_X>(&reg_file, bus, r8::REG_H);
  lookup_.at(0x75) = make_unique<LD_HL_X>(&reg_file, bus, r8::REG_L);
  lookup_.at(0x77) = make_unique<LD_HL_X>(&reg_file, bus, r8::REG_A);
  lookup_.at(0x78) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_A, r8::REG_B);
  lookup_.at(0x79) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_A, r8::REG_C);
  lookup_.at(0x7A) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_A, r8::REG_D);
  lookup_.at(0x7B) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_A, r8::REG_E);
  lookup_.at(0x7C) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_A, r8::REG_H);
  lookup_.at(0x7D) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_A, r8::REG_L);
  lookup_.at(0x7E) = make_unique<LD_X_HL>(&reg_file, bus, r8::REG_A);
  lookup_.at(0x7F) = make_unique<LD_X_Y>(&reg_file, bus, r8::REG_A, r8::REG_A);

  lookup_.at(0xC1) = make_unique<POP_XX>(&reg_file, bus, r16::REG_BC);
  lookup_.at(0xC5) = make_unique<PUSH_XX>(&reg_file, bus, r16::REG_BC);

  lookup_.at(0xD1) = make_unique<POP_XX>(&reg_file, bus, r16::REG_DE);
  lookup_.at(0xD5) = make_unique<PUSH_XX>(&reg_file, bus, r16::REG_DE);

  lookup_.at(0xE0) = make_unique<LDH_imm8_A>(&reg_file, bus);
  lookup_.at(0xE1) = make_unique<POP_XX>(&reg_file, bus, r16::REG_HL);
  lookup_.at(0xE2) = make_unique<LDH_C_A>(&reg_file, bus);
  lookup_.at(0xE5) = make_unique<PUSH_XX>(&reg_file, bus, r16::REG_HL);
  lookup_.at(0xEA) = make_unique<LD_imm16_A>(&reg_file, bus);

  lookup_.at(0xF0) = make_unique<LDH_A_imm8>(&reg_file, bus);
  lookup_.at(0xF1) = make_unique<POP_XX>(&reg_file, bus, r16::REG_AF);
  lookup_.at(0xF2) = make_unique<LDH_A_C>(&reg_file, bus);
  lookup_.at(0xF5) = make_unique<PUSH_XX>(&reg_file, bus, r16::REG_AF);
  lookup_.at(0xF9) = make_unique<LD_SP_HL>(&reg_file, bus);
  lookup_.at(0xFA) = make_unique<LD_A_imm16>(&reg_file, bus);
}
