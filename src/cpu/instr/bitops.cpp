#include "cpu/instr/bitops.hpp"
#include "cpu/instr/instr.hpp"
#include "cpu/lr35902.hpp"

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

std::tuple<std::size_t, std::size_t> CB_PREFIX::step() {
  unique_ptr<Instruction> &ins = lookup.at(op);

  // Handle un-implemented opcodes - unlikely because CB doesn't have illegal
  // opcodes at all, but eh. Whatever.
  if (!ins) [[unlikely]] {
    std::ostringstream oss;
    oss << "Unimplemended opcode (0xCB): 0x" << std::uppercase << std::hex
        << std::setw(2) << std::setfill('0') << static_cast<int>(op);
    throw std::logic_error(oss.str());
  }

  // Parse instruction operands and execute just like in CPU code
  ins->parse();
  return ins->step();
}

void CB_PREFIX::parse() { op = bus->read_byte(reg_file->reg_pc++); }

void LR35902::init_bitops(lookup_table_t &lookup) {
  lookup.at(0x07) = make_unique<RLCA>(&reg_file, bus);
  lookup.at(0x0F) = make_unique<RRCA>(&reg_file, bus);
  lookup.at(0x17) = make_unique<RLA>(&reg_file, bus);
  lookup.at(0x1F) = make_unique<RRA>(&reg_file, bus);
  lookup.at(0xCB) = make_unique<CB_PREFIX>(&reg_file, bus);
}

void CB_PREFIX::init_cb_prefix(lookup_table_t &lookup) {
  lookup.at(0x00) = make_unique<RLC_X<r8::REG_B>>(reg_file, bus);
  lookup.at(0x01) = make_unique<RLC_X<r8::REG_C>>(reg_file, bus);
  lookup.at(0x02) = make_unique<RLC_X<r8::REG_D>>(reg_file, bus);
  lookup.at(0x03) = make_unique<RLC_X<r8::REG_E>>(reg_file, bus);
  lookup.at(0x04) = make_unique<RLC_X<r8::REG_H>>(reg_file, bus);
  lookup.at(0x05) = make_unique<RLC_X<r8::REG_L>>(reg_file, bus);
  lookup.at(0x06) = make_unique<RLC_HL>(reg_file, bus);
  lookup.at(0x07) = make_unique<RLC_X<r8::REG_A>>(reg_file, bus);

  lookup.at(0x08) = make_unique<RRC_X<r8::REG_B>>(reg_file, bus);
  lookup.at(0x09) = make_unique<RRC_X<r8::REG_C>>(reg_file, bus);
  lookup.at(0x0A) = make_unique<RRC_X<r8::REG_D>>(reg_file, bus);
  lookup.at(0x0B) = make_unique<RRC_X<r8::REG_E>>(reg_file, bus);
  lookup.at(0x0C) = make_unique<RRC_X<r8::REG_H>>(reg_file, bus);
  lookup.at(0x0D) = make_unique<RRC_X<r8::REG_L>>(reg_file, bus);
  lookup.at(0x0E) = make_unique<RRC_HL>(reg_file, bus);
  lookup.at(0x0F) = make_unique<RRC_X<r8::REG_A>>(reg_file, bus);

  lookup.at(0x10) = make_unique<RL_X<r8::REG_B>>(reg_file, bus);
  lookup.at(0x11) = make_unique<RL_X<r8::REG_C>>(reg_file, bus);
  lookup.at(0x12) = make_unique<RL_X<r8::REG_D>>(reg_file, bus);
  lookup.at(0x13) = make_unique<RL_X<r8::REG_E>>(reg_file, bus);
  lookup.at(0x14) = make_unique<RL_X<r8::REG_H>>(reg_file, bus);
  lookup.at(0x15) = make_unique<RL_X<r8::REG_L>>(reg_file, bus);
  lookup.at(0x16) = make_unique<RL_HL>(reg_file, bus);
  lookup.at(0x17) = make_unique<RL_X<r8::REG_A>>(reg_file, bus);

  lookup.at(0x18) = make_unique<RR_X<r8::REG_B>>(reg_file, bus);
  lookup.at(0x19) = make_unique<RR_X<r8::REG_C>>(reg_file, bus);
  lookup.at(0x1a) = make_unique<RR_X<r8::REG_D>>(reg_file, bus);
  lookup.at(0x1b) = make_unique<RR_X<r8::REG_E>>(reg_file, bus);
  lookup.at(0x1c) = make_unique<RR_X<r8::REG_H>>(reg_file, bus);
  lookup.at(0x1d) = make_unique<RR_X<r8::REG_L>>(reg_file, bus);
  lookup.at(0x1e) = make_unique<RR_HL>(reg_file, bus);
  lookup.at(0x1f) = make_unique<RR_X<r8::REG_A>>(reg_file, bus);

  lookup.at(0x20) = make_unique<SLA_X<r8::REG_B>>(reg_file, bus);
  lookup.at(0x21) = make_unique<SLA_X<r8::REG_C>>(reg_file, bus);
  lookup.at(0x22) = make_unique<SLA_X<r8::REG_D>>(reg_file, bus);
  lookup.at(0x23) = make_unique<SLA_X<r8::REG_E>>(reg_file, bus);
  lookup.at(0x24) = make_unique<SLA_X<r8::REG_H>>(reg_file, bus);
  lookup.at(0x25) = make_unique<SLA_X<r8::REG_L>>(reg_file, bus);
  lookup.at(0x26) = make_unique<SLA_HL>(reg_file, bus);
  lookup.at(0x27) = make_unique<SLA_X<r8::REG_A>>(reg_file, bus);

  lookup.at(0x28) = make_unique<SRA_X<r8::REG_B>>(reg_file, bus);
  lookup.at(0x29) = make_unique<SRA_X<r8::REG_C>>(reg_file, bus);
  lookup.at(0x2a) = make_unique<SRA_X<r8::REG_D>>(reg_file, bus);
  lookup.at(0x2b) = make_unique<SRA_X<r8::REG_E>>(reg_file, bus);
  lookup.at(0x2c) = make_unique<SRA_X<r8::REG_H>>(reg_file, bus);
  lookup.at(0x2d) = make_unique<SRA_X<r8::REG_L>>(reg_file, bus);
  lookup.at(0x2e) = make_unique<SRA_HL>(reg_file, bus);
  lookup.at(0x2f) = make_unique<SRA_X<r8::REG_A>>(reg_file, bus);

  lookup.at(0x30) = make_unique<SWAP_X<r8::REG_B>>(reg_file, bus);
  lookup.at(0x31) = make_unique<SWAP_X<r8::REG_C>>(reg_file, bus);
  lookup.at(0x32) = make_unique<SWAP_X<r8::REG_D>>(reg_file, bus);
  lookup.at(0x33) = make_unique<SWAP_X<r8::REG_E>>(reg_file, bus);
  lookup.at(0x34) = make_unique<SWAP_X<r8::REG_H>>(reg_file, bus);
  lookup.at(0x35) = make_unique<SWAP_X<r8::REG_L>>(reg_file, bus);
  lookup.at(0x36) = make_unique<SWAP_HL>(reg_file, bus);
  lookup.at(0x37) = make_unique<SWAP_X<r8::REG_A>>(reg_file, bus);

  lookup.at(0x38) = make_unique<SRL_X<r8::REG_B>>(reg_file, bus);
  lookup.at(0x39) = make_unique<SRL_X<r8::REG_C>>(reg_file, bus);
  lookup.at(0x3a) = make_unique<SRL_X<r8::REG_D>>(reg_file, bus);
  lookup.at(0x3b) = make_unique<SRL_X<r8::REG_E>>(reg_file, bus);
  lookup.at(0x3c) = make_unique<SRL_X<r8::REG_H>>(reg_file, bus);
  lookup.at(0x3d) = make_unique<SRL_X<r8::REG_L>>(reg_file, bus);
  lookup.at(0x3e) = make_unique<SRL_HL>(reg_file, bus);
  lookup.at(0x3f) = make_unique<SRL_X<r8::REG_A>>(reg_file, bus);
}
