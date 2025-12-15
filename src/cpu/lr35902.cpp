#include <cpu/registers/register.hpp>
#include <cpu/registers/flags.hpp>
#include <cpu/lr35902.hpp>

LR35902::LR35902(AddressBus *bus_ptr) : bus(bus_ptr) {
  reg_file.reg_af = CpuFlagsRegister();
  reg_file.reg_bc = CpuRegister();
  reg_file.reg_de = CpuRegister();
  reg_file.reg_hl = CpuRegister();
  reg_file.reg_sp = CpuRegister();
  reg_file.reg_pc = 0x0000;
  lookup = { };

  /* Configure opcode lookup tables */
  init_moves(lookup);
}

void LR35902::step() {
  // TODO
}
