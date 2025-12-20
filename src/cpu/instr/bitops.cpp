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
  // opcodes at all, but eh
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

void CB_PREFIX::init_cb_prefix(lookup_table_t &lookup) {}
