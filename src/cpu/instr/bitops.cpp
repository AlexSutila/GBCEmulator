#include "cpu/instr/bitops.hpp"
#include "cpu/lr35902.hpp"

#include <array>
#include <memory>

using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
using r16 = Register16Bit;
using r8 = Register8Bit;
using std::make_unique;

void LR35902::init_bitops(lookup_table_t &lookup) {
  lookup.at(0x07) = make_unique<RLCA>(&reg_file, bus);
  lookup.at(0x0F) = make_unique<RRCA>(&reg_file, bus);
  lookup.at(0x17) = make_unique<RLA>(&reg_file, bus);
  lookup.at(0x1F) = make_unique<RRA>(&reg_file, bus);
}
