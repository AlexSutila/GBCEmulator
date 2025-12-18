#include "cpu/lr35902.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/register.hpp"
#include "memory/mmio.hpp"

#include <cassert>
#include <iomanip>
#include <ios>
#include <sstream>
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
  ime.step();

  // Decode instruction
  const byte_t op = bus->read_byte(reg_file.reg_pc++);
  std::unique_ptr<Instruction> &ins = lookup.at(op);

  // Handle un-implemented opcodes
  if (!ins) {
    std::ostringstream oss;
    oss << "Unimplemented opcode: 0x" << std::uppercase << std::hex
        << std::setw(2) << std::setfill('0') << static_cast<int>(op);
    throw std::logic_error(oss.str());
  }

  // Parse instruction operands
  ins->parse();

  // Execute instruction
  ins->step();
}
