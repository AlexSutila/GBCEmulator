#include "cpu/instr/control.hpp"
#include "cpu/lr35902.hpp"

#include <array>
#include <memory>

using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
using r16 = Register16Bit;
using r8 = Register8Bit;
using std::make_unique;

void LR35902::init_control(lookup_table_t &lookup, runtime_sys_info &sys) {
  lookup.at(0x00) = make_unique<NOP>(&reg_file, bus);
  lookup.at(0x10) = make_unique<STOP>(&reg_file, bus, sys);
  lookup.at(0x2F) = make_unique<CPL>(&reg_file, bus);
  lookup.at(0x37) = make_unique<SCF>(&reg_file, bus);
  lookup.at(0x3F) = make_unique<CCF>(&reg_file, bus);
  // We pass in all this stuff to simulate the HALT bug
  lookup.at(0x76) = make_unique<HALT>(&reg_file, bus, ime, if_reg, ie_reg,
                                      halt_bug_triggered, sys);
  lookup.at(0xF3) = make_unique<DI>(&reg_file, bus, &ime);
  lookup.at(0xFB) = make_unique<EI>(&reg_file, bus, &ime);
}
