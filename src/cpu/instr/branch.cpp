#include <array>
#include <cpu/instr/branch.hpp>
#include <cpu/lr35902.hpp>
#include <memory>

using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
using flags = StatusFlagMask;
using r16 = Register16Bit;
using r8 = Register8Bit;
using std::make_unique;

void LR35902::init_branch(lookup_table_t &lookup) {
  lookup.at(0x18) = make_unique<JR_imm8>(&reg_file, bus);

  lookup.at(0x20) = make_unique<JR_cond_imm8<flags::FLAG_Z_MASK, false>>(&reg_file, bus);
  lookup.at(0x28) = make_unique<JR_cond_imm8<flags::FLAG_Z_MASK, true>>(&reg_file, bus);

  lookup.at(0x30) = make_unique<JR_cond_imm8<flags::FLAG_C_MASK, false>>(&reg_file, bus);
  lookup.at(0x38) = make_unique<JR_cond_imm8<flags::FLAG_C_MASK, true>>(&reg_file, bus);

  lookup.at(0xC0) = make_unique<RET_cond<flags::FLAG_Z_MASK, false>>(&reg_file, bus);
  lookup.at(0xC2) = make_unique<JP_cond_imm16<flags::FLAG_Z_MASK, false>>(&reg_file, bus);
  lookup.at(0xC3) = make_unique<JP_imm16>(&reg_file, bus);
  lookup.at(0xC4) = make_unique<CALL_cond_imm16<flags::FLAG_Z_MASK, false>>(&reg_file, bus);
  lookup.at(0xC7) = make_unique<RST_vec<0x00>>(&reg_file, bus);
  lookup.at(0xC8) = make_unique<RET_cond<flags::FLAG_Z_MASK, true>>(&reg_file, bus);
  lookup.at(0xC9) = make_unique<RET>(&reg_file, bus);
  lookup.at(0xCA) = make_unique<JP_cond_imm16<flags::FLAG_Z_MASK, true>>(&reg_file, bus);
  lookup.at(0xCC) = make_unique<CALL_cond_imm16<flags::FLAG_Z_MASK, true>>(&reg_file, bus);
  lookup.at(0xCD) = make_unique<CALL_imm16>(&reg_file, bus);
  lookup.at(0xCF) = make_unique<RST_vec<0x08>>(&reg_file, bus);

  lookup.at(0xD0) = make_unique<RET_cond<flags::FLAG_C_MASK, false>>(&reg_file, bus);
  lookup.at(0xD2) = make_unique<JP_cond_imm16<flags::FLAG_C_MASK, false>>(&reg_file, bus);
  lookup.at(0xD4) = make_unique<CALL_cond_imm16<flags::FLAG_C_MASK, false>>(&reg_file, bus);
  lookup.at(0xD7) = make_unique<RST_vec<0x10>>(&reg_file, bus);
  lookup.at(0xD8) = make_unique<RET_cond<flags::FLAG_C_MASK, true>>(&reg_file, bus);
  lookup.at(0xD9) = make_unique<RETI>(&reg_file, bus, &ime);
  lookup.at(0xDA) = make_unique<JP_cond_imm16<flags::FLAG_C_MASK, true>>(&reg_file, bus);
  lookup.at(0xDC) = make_unique<CALL_cond_imm16<flags::FLAG_C_MASK, true>>(&reg_file, bus);
  lookup.at(0xDF) = make_unique<RST_vec<0x18>>(&reg_file, bus);

  lookup.at(0xE7) = make_unique<RST_vec<0x20>>(&reg_file, bus);
  lookup.at(0xE9) = make_unique<JP_HL>(&reg_file, bus);
  lookup.at(0xEF) = make_unique<RST_vec<0x28>>(&reg_file, bus);

  lookup.at(0xF7) = make_unique<RST_vec<0x30>>(&reg_file, bus);
  lookup.at(0xFF) = make_unique<RST_vec<0x38>>(&reg_file, bus);
}
