#include <cassert>
#include <cpu/interrupts.hpp>
#include <cpu/lr35902.hpp>
#include <cpu/registers/flags.hpp>
#include <cpu/registers/register.hpp>
#include <memory/mmio.hpp>
#include <stdexcept>

LR35902::LR35902(AddressBus *bus_ptr) : bus(bus_ptr) {
  using ioregs = IORegisterMapping;

  reg_file.reg_af = CpuFlagsRegister();
  reg_file.reg_bc = CpuRegister();
  reg_file.reg_de = CpuRegister();
  reg_file.reg_hl = CpuRegister();
  reg_file.reg_sp = CpuRegister();
  reg_file.reg_pc = 0x0000;
  lookup = {};

  /* Configure opcode lookup tables */
  init_alu(lookup);
  init_branch(lookup);
  init_control(lookup);
  init_moves(lookup);

  /* Configure interrupts */
  ime = InterruptMasterEnable();
  auto *reg = bus->get_mmio(ioregs::MMIO_INT_ENABLE);
  if (!(ie_reg = dynamic_cast<InterruptBits *>(reg)))
    throw std::logic_error("Failed to connect MMIO_INT_ENABLE");
  reg = bus->get_mmio(ioregs::MMIO_INT_FLAGS);
  if (!(if_reg = dynamic_cast<InterruptBits *>(reg)))
    throw std::logic_error("Failed to connect MMIO_INT_FLAGS");
  assert(ie_reg != nullptr && if_reg != nullptr);
}

void LR35902::step() {
  // TODO
}
