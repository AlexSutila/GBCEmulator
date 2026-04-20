#include "cpu/instr/bitops.hpp"
#include "cpu/instr/instr.hpp"
#include "cpu/lr35902.hpp"
#include "format.hpp"

#include <array>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
using r16 = Register16Bit;
using r8 = Register8Bit;
using std::make_unique, std::unique_ptr;

CB_PREFIX::CB_PREFIX(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
    : Instruction(reg_file_ptr, bus_ptr) {
  lookup = {};
  init_cb_prefix(lookup);
}

std::size_t CB_PREFIX::exec() {
  const unique_ptr<Instruction> &ins = lookup.at(op);

  // Handle un-implemented opcodes - unlikely because CB doesn't have illegal
  // opcodes at all, but eh. Whatever.
  if (!ins) [[unlikely]] {
    std::ostringstream oss;
    oss << "Unimplemented opcode (0xCB): 0x" << std::uppercase << std::hex << std::setw(2)
        << std::setfill('0') << static_cast<int>(op);
    throw std::logic_error(oss.str());
  }

  // Parse instruction operands and execute just like in CPU code
  return ins->exec();
}

std::size_t CB_PREFIX::mem_access_t_cycle() {
  const unique_ptr<Instruction> &ins = lookup.at(op);
  return ins->mem_access_t_cycle();
}

// TODO: This could fuck up royally but we ball lmao
std::string CB_PREFIX::describe() { return IroGB::format("(CB) {}", lookup.at(op)->describe()); }

InstructionTiming CB_PREFIX::parse() {
  op = bus->read_byte(reg_file->reg_pc++);
  const unique_ptr<Instruction> &ins = lookup.at(op);
  return ins->parse();
}

void LR35902::init_bitops(lookup_table_t &lookup_) {
  lookup_.at(0x07) = make_unique<RLCA>(&reg_file, bus);
  lookup_.at(0x0F) = make_unique<RRCA>(&reg_file, bus);
  lookup_.at(0x17) = make_unique<RLA>(&reg_file, bus);
  lookup_.at(0x1F) = make_unique<RRA>(&reg_file, bus);
  lookup_.at(0xCB) = make_unique<CB_PREFIX>(&reg_file, bus);
}

void CB_PREFIX::init_cb_prefix(lookup_table_t &lookup_) const {
  lookup_.at(0x00) = make_unique<RLC_X<r8::REG_B>>(reg_file, bus);
  lookup_.at(0x01) = make_unique<RLC_X<r8::REG_C>>(reg_file, bus);
  lookup_.at(0x02) = make_unique<RLC_X<r8::REG_D>>(reg_file, bus);
  lookup_.at(0x03) = make_unique<RLC_X<r8::REG_E>>(reg_file, bus);
  lookup_.at(0x04) = make_unique<RLC_X<r8::REG_H>>(reg_file, bus);
  lookup_.at(0x05) = make_unique<RLC_X<r8::REG_L>>(reg_file, bus);
  lookup_.at(0x06) = make_unique<RLC_HL>(reg_file, bus);
  lookup_.at(0x07) = make_unique<RLC_X<r8::REG_A>>(reg_file, bus);

  lookup_.at(0x08) = make_unique<RRC_X<r8::REG_B>>(reg_file, bus);
  lookup_.at(0x09) = make_unique<RRC_X<r8::REG_C>>(reg_file, bus);
  lookup_.at(0x0A) = make_unique<RRC_X<r8::REG_D>>(reg_file, bus);
  lookup_.at(0x0B) = make_unique<RRC_X<r8::REG_E>>(reg_file, bus);
  lookup_.at(0x0C) = make_unique<RRC_X<r8::REG_H>>(reg_file, bus);
  lookup_.at(0x0D) = make_unique<RRC_X<r8::REG_L>>(reg_file, bus);
  lookup_.at(0x0E) = make_unique<RRC_HL>(reg_file, bus);
  lookup_.at(0x0F) = make_unique<RRC_X<r8::REG_A>>(reg_file, bus);

  lookup_.at(0x10) = make_unique<RL_X<r8::REG_B>>(reg_file, bus);
  lookup_.at(0x11) = make_unique<RL_X<r8::REG_C>>(reg_file, bus);
  lookup_.at(0x12) = make_unique<RL_X<r8::REG_D>>(reg_file, bus);
  lookup_.at(0x13) = make_unique<RL_X<r8::REG_E>>(reg_file, bus);
  lookup_.at(0x14) = make_unique<RL_X<r8::REG_H>>(reg_file, bus);
  lookup_.at(0x15) = make_unique<RL_X<r8::REG_L>>(reg_file, bus);
  lookup_.at(0x16) = make_unique<RL_HL>(reg_file, bus);
  lookup_.at(0x17) = make_unique<RL_X<r8::REG_A>>(reg_file, bus);

  lookup_.at(0x18) = make_unique<RR_X<r8::REG_B>>(reg_file, bus);
  lookup_.at(0x19) = make_unique<RR_X<r8::REG_C>>(reg_file, bus);
  lookup_.at(0x1a) = make_unique<RR_X<r8::REG_D>>(reg_file, bus);
  lookup_.at(0x1b) = make_unique<RR_X<r8::REG_E>>(reg_file, bus);
  lookup_.at(0x1c) = make_unique<RR_X<r8::REG_H>>(reg_file, bus);
  lookup_.at(0x1d) = make_unique<RR_X<r8::REG_L>>(reg_file, bus);
  lookup_.at(0x1e) = make_unique<RR_HL>(reg_file, bus);
  lookup_.at(0x1f) = make_unique<RR_X<r8::REG_A>>(reg_file, bus);

  lookup_.at(0x20) = make_unique<SLA_X<r8::REG_B>>(reg_file, bus);
  lookup_.at(0x21) = make_unique<SLA_X<r8::REG_C>>(reg_file, bus);
  lookup_.at(0x22) = make_unique<SLA_X<r8::REG_D>>(reg_file, bus);
  lookup_.at(0x23) = make_unique<SLA_X<r8::REG_E>>(reg_file, bus);
  lookup_.at(0x24) = make_unique<SLA_X<r8::REG_H>>(reg_file, bus);
  lookup_.at(0x25) = make_unique<SLA_X<r8::REG_L>>(reg_file, bus);
  lookup_.at(0x26) = make_unique<SLA_HL>(reg_file, bus);
  lookup_.at(0x27) = make_unique<SLA_X<r8::REG_A>>(reg_file, bus);

  lookup_.at(0x28) = make_unique<SRA_X<r8::REG_B>>(reg_file, bus);
  lookup_.at(0x29) = make_unique<SRA_X<r8::REG_C>>(reg_file, bus);
  lookup_.at(0x2a) = make_unique<SRA_X<r8::REG_D>>(reg_file, bus);
  lookup_.at(0x2b) = make_unique<SRA_X<r8::REG_E>>(reg_file, bus);
  lookup_.at(0x2c) = make_unique<SRA_X<r8::REG_H>>(reg_file, bus);
  lookup_.at(0x2d) = make_unique<SRA_X<r8::REG_L>>(reg_file, bus);
  lookup_.at(0x2e) = make_unique<SRA_HL>(reg_file, bus);
  lookup_.at(0x2f) = make_unique<SRA_X<r8::REG_A>>(reg_file, bus);

  lookup_.at(0x30) = make_unique<SWAP_X<r8::REG_B>>(reg_file, bus);
  lookup_.at(0x31) = make_unique<SWAP_X<r8::REG_C>>(reg_file, bus);
  lookup_.at(0x32) = make_unique<SWAP_X<r8::REG_D>>(reg_file, bus);
  lookup_.at(0x33) = make_unique<SWAP_X<r8::REG_E>>(reg_file, bus);
  lookup_.at(0x34) = make_unique<SWAP_X<r8::REG_H>>(reg_file, bus);
  lookup_.at(0x35) = make_unique<SWAP_X<r8::REG_L>>(reg_file, bus);
  lookup_.at(0x36) = make_unique<SWAP_HL>(reg_file, bus);
  lookup_.at(0x37) = make_unique<SWAP_X<r8::REG_A>>(reg_file, bus);

  lookup_.at(0x38) = make_unique<SRL_X<r8::REG_B>>(reg_file, bus);
  lookup_.at(0x39) = make_unique<SRL_X<r8::REG_C>>(reg_file, bus);
  lookup_.at(0x3a) = make_unique<SRL_X<r8::REG_D>>(reg_file, bus);
  lookup_.at(0x3b) = make_unique<SRL_X<r8::REG_E>>(reg_file, bus);
  lookup_.at(0x3c) = make_unique<SRL_X<r8::REG_H>>(reg_file, bus);
  lookup_.at(0x3d) = make_unique<SRL_X<r8::REG_L>>(reg_file, bus);
  lookup_.at(0x3e) = make_unique<SRL_HL>(reg_file, bus);
  lookup_.at(0x3f) = make_unique<SRL_X<r8::REG_A>>(reg_file, bus);

  lookup_.at(0x40) = make_unique<BIT_N_X<0, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x41) = make_unique<BIT_N_X<0, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x42) = make_unique<BIT_N_X<0, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x43) = make_unique<BIT_N_X<0, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x44) = make_unique<BIT_N_X<0, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x45) = make_unique<BIT_N_X<0, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x46) = make_unique<BIT_N_HL<0>>(reg_file, bus);
  lookup_.at(0x47) = make_unique<BIT_N_X<0, r8::REG_A>>(reg_file, bus);
  lookup_.at(0x48) = make_unique<BIT_N_X<1, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x49) = make_unique<BIT_N_X<1, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x4a) = make_unique<BIT_N_X<1, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x4b) = make_unique<BIT_N_X<1, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x4c) = make_unique<BIT_N_X<1, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x4d) = make_unique<BIT_N_X<1, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x4e) = make_unique<BIT_N_HL<1>>(reg_file, bus);
  lookup_.at(0x4f) = make_unique<BIT_N_X<1, r8::REG_A>>(reg_file, bus);

  lookup_.at(0x50) = make_unique<BIT_N_X<2, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x51) = make_unique<BIT_N_X<2, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x52) = make_unique<BIT_N_X<2, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x53) = make_unique<BIT_N_X<2, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x54) = make_unique<BIT_N_X<2, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x55) = make_unique<BIT_N_X<2, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x56) = make_unique<BIT_N_HL<2>>(reg_file, bus);
  lookup_.at(0x57) = make_unique<BIT_N_X<2, r8::REG_A>>(reg_file, bus);
  lookup_.at(0x58) = make_unique<BIT_N_X<3, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x59) = make_unique<BIT_N_X<3, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x5a) = make_unique<BIT_N_X<3, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x5b) = make_unique<BIT_N_X<3, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x5c) = make_unique<BIT_N_X<3, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x5d) = make_unique<BIT_N_X<3, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x5e) = make_unique<BIT_N_HL<3>>(reg_file, bus);
  lookup_.at(0x5f) = make_unique<BIT_N_X<3, r8::REG_A>>(reg_file, bus);

  lookup_.at(0x60) = make_unique<BIT_N_X<4, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x61) = make_unique<BIT_N_X<4, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x62) = make_unique<BIT_N_X<4, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x63) = make_unique<BIT_N_X<4, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x64) = make_unique<BIT_N_X<4, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x65) = make_unique<BIT_N_X<4, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x66) = make_unique<BIT_N_HL<4>>(reg_file, bus);
  lookup_.at(0x67) = make_unique<BIT_N_X<4, r8::REG_A>>(reg_file, bus);
  lookup_.at(0x68) = make_unique<BIT_N_X<5, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x69) = make_unique<BIT_N_X<5, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x6a) = make_unique<BIT_N_X<5, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x6b) = make_unique<BIT_N_X<5, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x6c) = make_unique<BIT_N_X<5, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x6d) = make_unique<BIT_N_X<5, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x6e) = make_unique<BIT_N_HL<5>>(reg_file, bus);
  lookup_.at(0x6f) = make_unique<BIT_N_X<5, r8::REG_A>>(reg_file, bus);

  lookup_.at(0x70) = make_unique<BIT_N_X<6, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x71) = make_unique<BIT_N_X<6, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x72) = make_unique<BIT_N_X<6, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x73) = make_unique<BIT_N_X<6, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x74) = make_unique<BIT_N_X<6, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x75) = make_unique<BIT_N_X<6, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x76) = make_unique<BIT_N_HL<6>>(reg_file, bus);
  lookup_.at(0x77) = make_unique<BIT_N_X<6, r8::REG_A>>(reg_file, bus);
  lookup_.at(0x78) = make_unique<BIT_N_X<7, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x79) = make_unique<BIT_N_X<7, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x7a) = make_unique<BIT_N_X<7, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x7b) = make_unique<BIT_N_X<7, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x7c) = make_unique<BIT_N_X<7, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x7d) = make_unique<BIT_N_X<7, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x7e) = make_unique<BIT_N_HL<7>>(reg_file, bus);
  lookup_.at(0x7f) = make_unique<BIT_N_X<7, r8::REG_A>>(reg_file, bus);

  lookup_.at(0x80) = make_unique<RES_N_X<0, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x81) = make_unique<RES_N_X<0, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x82) = make_unique<RES_N_X<0, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x83) = make_unique<RES_N_X<0, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x84) = make_unique<RES_N_X<0, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x85) = make_unique<RES_N_X<0, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x86) = make_unique<RES_N_HL<0>>(reg_file, bus);
  lookup_.at(0x87) = make_unique<RES_N_X<0, r8::REG_A>>(reg_file, bus);
  lookup_.at(0x88) = make_unique<RES_N_X<1, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x89) = make_unique<RES_N_X<1, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x8a) = make_unique<RES_N_X<1, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x8b) = make_unique<RES_N_X<1, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x8c) = make_unique<RES_N_X<1, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x8d) = make_unique<RES_N_X<1, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x8e) = make_unique<RES_N_HL<1>>(reg_file, bus);
  lookup_.at(0x8f) = make_unique<RES_N_X<1, r8::REG_A>>(reg_file, bus);

  lookup_.at(0x90) = make_unique<RES_N_X<2, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x91) = make_unique<RES_N_X<2, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x92) = make_unique<RES_N_X<2, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x93) = make_unique<RES_N_X<2, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x94) = make_unique<RES_N_X<2, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x95) = make_unique<RES_N_X<2, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x96) = make_unique<RES_N_HL<2>>(reg_file, bus);
  lookup_.at(0x97) = make_unique<RES_N_X<2, r8::REG_A>>(reg_file, bus);
  lookup_.at(0x98) = make_unique<RES_N_X<3, r8::REG_B>>(reg_file, bus);
  lookup_.at(0x99) = make_unique<RES_N_X<3, r8::REG_C>>(reg_file, bus);
  lookup_.at(0x9a) = make_unique<RES_N_X<3, r8::REG_D>>(reg_file, bus);
  lookup_.at(0x9b) = make_unique<RES_N_X<3, r8::REG_E>>(reg_file, bus);
  lookup_.at(0x9c) = make_unique<RES_N_X<3, r8::REG_H>>(reg_file, bus);
  lookup_.at(0x9d) = make_unique<RES_N_X<3, r8::REG_L>>(reg_file, bus);
  lookup_.at(0x9e) = make_unique<RES_N_HL<3>>(reg_file, bus);
  lookup_.at(0x9f) = make_unique<RES_N_X<3, r8::REG_A>>(reg_file, bus);

  lookup_.at(0xa0) = make_unique<RES_N_X<4, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xa1) = make_unique<RES_N_X<4, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xa2) = make_unique<RES_N_X<4, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xa3) = make_unique<RES_N_X<4, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xa4) = make_unique<RES_N_X<4, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xa5) = make_unique<RES_N_X<4, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xa6) = make_unique<RES_N_HL<4>>(reg_file, bus);
  lookup_.at(0xa7) = make_unique<RES_N_X<4, r8::REG_A>>(reg_file, bus);
  lookup_.at(0xa8) = make_unique<RES_N_X<5, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xa9) = make_unique<RES_N_X<5, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xaa) = make_unique<RES_N_X<5, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xab) = make_unique<RES_N_X<5, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xac) = make_unique<RES_N_X<5, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xad) = make_unique<RES_N_X<5, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xae) = make_unique<RES_N_HL<5>>(reg_file, bus);
  lookup_.at(0xaf) = make_unique<RES_N_X<5, r8::REG_A>>(reg_file, bus);

  lookup_.at(0xb0) = make_unique<RES_N_X<6, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xb1) = make_unique<RES_N_X<6, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xb2) = make_unique<RES_N_X<6, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xb3) = make_unique<RES_N_X<6, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xb4) = make_unique<RES_N_X<6, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xb5) = make_unique<RES_N_X<6, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xb6) = make_unique<RES_N_HL<6>>(reg_file, bus);
  lookup_.at(0xb7) = make_unique<RES_N_X<6, r8::REG_A>>(reg_file, bus);
  lookup_.at(0xb8) = make_unique<RES_N_X<7, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xb9) = make_unique<RES_N_X<7, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xba) = make_unique<RES_N_X<7, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xbb) = make_unique<RES_N_X<7, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xbc) = make_unique<RES_N_X<7, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xbd) = make_unique<RES_N_X<7, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xbe) = make_unique<RES_N_HL<7>>(reg_file, bus);
  lookup_.at(0xbf) = make_unique<RES_N_X<7, r8::REG_A>>(reg_file, bus);

  lookup_.at(0xc0) = make_unique<SET_N_X<0, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xc1) = make_unique<SET_N_X<0, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xc2) = make_unique<SET_N_X<0, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xc3) = make_unique<SET_N_X<0, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xc4) = make_unique<SET_N_X<0, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xc5) = make_unique<SET_N_X<0, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xc6) = make_unique<SET_N_HL<0>>(reg_file, bus);
  lookup_.at(0xc7) = make_unique<SET_N_X<0, r8::REG_A>>(reg_file, bus);
  lookup_.at(0xc8) = make_unique<SET_N_X<1, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xc9) = make_unique<SET_N_X<1, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xca) = make_unique<SET_N_X<1, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xcb) = make_unique<SET_N_X<1, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xcc) = make_unique<SET_N_X<1, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xcd) = make_unique<SET_N_X<1, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xce) = make_unique<SET_N_HL<1>>(reg_file, bus);
  lookup_.at(0xcf) = make_unique<SET_N_X<1, r8::REG_A>>(reg_file, bus);

  lookup_.at(0xd0) = make_unique<SET_N_X<2, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xd1) = make_unique<SET_N_X<2, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xd2) = make_unique<SET_N_X<2, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xd3) = make_unique<SET_N_X<2, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xd4) = make_unique<SET_N_X<2, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xd5) = make_unique<SET_N_X<2, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xd6) = make_unique<SET_N_HL<2>>(reg_file, bus);
  lookup_.at(0xd7) = make_unique<SET_N_X<2, r8::REG_A>>(reg_file, bus);
  lookup_.at(0xd8) = make_unique<SET_N_X<3, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xd9) = make_unique<SET_N_X<3, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xda) = make_unique<SET_N_X<3, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xdb) = make_unique<SET_N_X<3, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xdc) = make_unique<SET_N_X<3, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xdd) = make_unique<SET_N_X<3, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xde) = make_unique<SET_N_HL<3>>(reg_file, bus);
  lookup_.at(0xdf) = make_unique<SET_N_X<3, r8::REG_A>>(reg_file, bus);

  lookup_.at(0xe0) = make_unique<SET_N_X<4, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xe1) = make_unique<SET_N_X<4, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xe2) = make_unique<SET_N_X<4, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xe3) = make_unique<SET_N_X<4, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xe4) = make_unique<SET_N_X<4, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xe5) = make_unique<SET_N_X<4, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xe6) = make_unique<SET_N_HL<4>>(reg_file, bus);
  lookup_.at(0xe7) = make_unique<SET_N_X<4, r8::REG_A>>(reg_file, bus);
  lookup_.at(0xe8) = make_unique<SET_N_X<5, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xe9) = make_unique<SET_N_X<5, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xea) = make_unique<SET_N_X<5, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xeb) = make_unique<SET_N_X<5, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xec) = make_unique<SET_N_X<5, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xed) = make_unique<SET_N_X<5, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xee) = make_unique<SET_N_HL<5>>(reg_file, bus);
  lookup_.at(0xef) = make_unique<SET_N_X<5, r8::REG_A>>(reg_file, bus);

  lookup_.at(0xf0) = make_unique<SET_N_X<6, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xf1) = make_unique<SET_N_X<6, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xf2) = make_unique<SET_N_X<6, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xf3) = make_unique<SET_N_X<6, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xf4) = make_unique<SET_N_X<6, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xf5) = make_unique<SET_N_X<6, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xf6) = make_unique<SET_N_HL<6>>(reg_file, bus);
  lookup_.at(0xf7) = make_unique<SET_N_X<6, r8::REG_A>>(reg_file, bus);
  lookup_.at(0xf8) = make_unique<SET_N_X<7, r8::REG_B>>(reg_file, bus);
  lookup_.at(0xf9) = make_unique<SET_N_X<7, r8::REG_C>>(reg_file, bus);
  lookup_.at(0xfa) = make_unique<SET_N_X<7, r8::REG_D>>(reg_file, bus);
  lookup_.at(0xfb) = make_unique<SET_N_X<7, r8::REG_E>>(reg_file, bus);
  lookup_.at(0xfc) = make_unique<SET_N_X<7, r8::REG_H>>(reg_file, bus);
  lookup_.at(0xfd) = make_unique<SET_N_X<7, r8::REG_L>>(reg_file, bus);
  lookup_.at(0xfe) = make_unique<SET_N_HL<7>>(reg_file, bus);
  lookup_.at(0xff) = make_unique<SET_N_X<7, r8::REG_A>>(reg_file, bus);
}
