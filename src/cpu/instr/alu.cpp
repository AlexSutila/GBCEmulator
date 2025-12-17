#include <array>
#include <cpu/instr/alu.hpp>
#include <cpu/lr35902.hpp>
#include <memory>

using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
using r16 = Register16Bit;
using r8 = Register8Bit;
using std::make_unique;

void LR35902::init_alu(lookup_table_t &lookup) {
  lookup.at(0x04) = make_unique<INC_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0x05) = make_unique<DEC_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0x0C) = make_unique<INC_X<r8::REG_C>>(&reg_file, bus);
  lookup.at(0x0D) = make_unique<DEC_X<r8::REG_C>>(&reg_file, bus);

  lookup.at(0x14) = make_unique<INC_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0x15) = make_unique<DEC_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0x1C) = make_unique<INC_X<r8::REG_E>>(&reg_file, bus);
  lookup.at(0x1D) = make_unique<DEC_X<r8::REG_E>>(&reg_file, bus);

  lookup.at(0x24) = make_unique<INC_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0x25) = make_unique<DEC_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0x27) = make_unique<DAA>(&reg_file, bus);
  lookup.at(0x2C) = make_unique<INC_X<r8::REG_L>>(&reg_file, bus);
  lookup.at(0x2D) = make_unique<DEC_X<r8::REG_L>>(&reg_file, bus);

  lookup.at(0x34) = make_unique<INC_HL>(&reg_file, bus);
  lookup.at(0x35) = make_unique<DEC_HL>(&reg_file, bus);
  lookup.at(0x3C) = make_unique<INC_X<r8::REG_A>>(&reg_file, bus);
  lookup.at(0x3D) = make_unique<DEC_X<r8::REG_A>>(&reg_file, bus);

  lookup.at(0x80) = make_unique<ADD_A_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0x81) = make_unique<ADD_A_X<r8::REG_C>>(&reg_file, bus);
  lookup.at(0x82) = make_unique<ADD_A_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0x83) = make_unique<ADD_A_X<r8::REG_E>>(&reg_file, bus);
  lookup.at(0x84) = make_unique<ADD_A_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0x85) = make_unique<ADD_A_X<r8::REG_L>>(&reg_file, bus);
  lookup.at(0x86) = make_unique<ADD_A_HL>(&reg_file, bus);
  lookup.at(0x87) = make_unique<ADD_A_X<r8::REG_A>>(&reg_file, bus);
  lookup.at(0x88) = make_unique<ADC_A_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0x89) = make_unique<ADC_A_X<r8::REG_C>>(&reg_file, bus);
  lookup.at(0x8A) = make_unique<ADC_A_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0x8B) = make_unique<ADC_A_X<r8::REG_E>>(&reg_file, bus);
  lookup.at(0x8C) = make_unique<ADC_A_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0x8D) = make_unique<ADC_A_X<r8::REG_L>>(&reg_file, bus);
  lookup.at(0x8E) = make_unique<ADC_A_HL>(&reg_file, bus);
  lookup.at(0x8F) = make_unique<ADC_A_X<r8::REG_A>>(&reg_file, bus);

  lookup.at(0x90) = make_unique<SUB_A_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0x91) = make_unique<SUB_A_X<r8::REG_C>>(&reg_file, bus);
  lookup.at(0x92) = make_unique<SUB_A_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0x93) = make_unique<SUB_A_X<r8::REG_E>>(&reg_file, bus);
  lookup.at(0x94) = make_unique<SUB_A_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0x95) = make_unique<SUB_A_X<r8::REG_L>>(&reg_file, bus);
  lookup.at(0x96) = make_unique<SUB_A_HL>(&reg_file, bus);
  lookup.at(0x97) = make_unique<SUB_A_X<r8::REG_A>>(&reg_file, bus);
  lookup.at(0x98) = make_unique<SBC_A_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0x99) = make_unique<SBC_A_X<r8::REG_C>>(&reg_file, bus);
  lookup.at(0x9A) = make_unique<SBC_A_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0x9B) = make_unique<SBC_A_X<r8::REG_E>>(&reg_file, bus);
  lookup.at(0x9C) = make_unique<SBC_A_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0x9D) = make_unique<SBC_A_X<r8::REG_L>>(&reg_file, bus);
  lookup.at(0x9E) = make_unique<SBC_A_HL>(&reg_file, bus);
  lookup.at(0x9F) = make_unique<SBC_A_X<r8::REG_A>>(&reg_file, bus);

  lookup.at(0xA0) = make_unique<AND_A_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0xA1) = make_unique<AND_A_X<r8::REG_C>>(&reg_file, bus);
  lookup.at(0xA2) = make_unique<AND_A_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0xA3) = make_unique<AND_A_X<r8::REG_E>>(&reg_file, bus);
  lookup.at(0xA4) = make_unique<AND_A_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0xA5) = make_unique<AND_A_X<r8::REG_L>>(&reg_file, bus);
  lookup.at(0xA6) = make_unique<AND_A_HL>(&reg_file, bus);
  lookup.at(0xA7) = make_unique<AND_A_X<r8::REG_A>>(&reg_file, bus);
  lookup.at(0xA8) = make_unique<XOR_A_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0xA9) = make_unique<XOR_A_X<r8::REG_C>>(&reg_file, bus);
  lookup.at(0xAA) = make_unique<XOR_A_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0xAB) = make_unique<XOR_A_X<r8::REG_E>>(&reg_file, bus);
  lookup.at(0xAC) = make_unique<XOR_A_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0xAD) = make_unique<XOR_A_X<r8::REG_L>>(&reg_file, bus);
  lookup.at(0xAE) = make_unique<XOR_A_HL>(&reg_file, bus);
  lookup.at(0xAF) = make_unique<XOR_A_X<r8::REG_A>>(&reg_file, bus);

  lookup.at(0xB0) = make_unique<OR_A_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0xB1) = make_unique<OR_A_X<r8::REG_C>>(&reg_file, bus);
  lookup.at(0xB2) = make_unique<OR_A_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0xB3) = make_unique<OR_A_X<r8::REG_E>>(&reg_file, bus);
  lookup.at(0xB4) = make_unique<OR_A_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0xB5) = make_unique<OR_A_X<r8::REG_L>>(&reg_file, bus);
  lookup.at(0xB6) = make_unique<OR_A_HL>(&reg_file, bus);
  lookup.at(0xB7) = make_unique<OR_A_X<r8::REG_A>>(&reg_file, bus);
  lookup.at(0xB8) = make_unique<CP_A_X<r8::REG_B>>(&reg_file, bus);
  lookup.at(0xB9) = make_unique<CP_A_X<r8::REG_C>>(&reg_file, bus);
  lookup.at(0xBA) = make_unique<CP_A_X<r8::REG_D>>(&reg_file, bus);
  lookup.at(0xBB) = make_unique<CP_A_X<r8::REG_E>>(&reg_file, bus);
  lookup.at(0xBC) = make_unique<CP_A_X<r8::REG_H>>(&reg_file, bus);
  lookup.at(0xBD) = make_unique<CP_A_X<r8::REG_L>>(&reg_file, bus);
  lookup.at(0xBE) = make_unique<CP_A_HL>(&reg_file, bus);
  lookup.at(0xBF) = make_unique<CP_A_X<r8::REG_A>>(&reg_file, bus);

  lookup.at(0xC6) = make_unique<ADD_A_imm8>(&reg_file, bus);
  lookup.at(0xCE) = make_unique<ADC_A_imm8>(&reg_file, bus);

  lookup.at(0xD6) = make_unique<SUB_A_imm8>(&reg_file, bus);
  lookup.at(0xDE) = make_unique<SBC_A_imm8>(&reg_file, bus);

  lookup.at(0xE6) = make_unique<AND_A_imm8>(&reg_file, bus);
  lookup.at(0xEE) = make_unique<XOR_A_imm8>(&reg_file, bus);

  lookup.at(0xF6) = make_unique<OR_A_imm8>(&reg_file, bus);
  lookup.at(0xFE) = make_unique<CP_A_imm8>(&reg_file, bus);
}
