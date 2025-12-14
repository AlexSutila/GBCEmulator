#ifndef __LR35902_H
#define __LR35902_H

#include <cpu/registers/flags.hpp>
#include <cpu/registers/register.hpp>
#include <emu_types.hpp>
#include <memory/bus.hpp>

class LR35902 {
public:
  LR35902(AddressBus *bus_ptr);
  void step();

private:
  AddressBus *const bus;

  /* LR35902 Register File */
  struct {
    CpuRegister reg_bc, reg_de, reg_hl;
    CpuFlagsRegister reg_af;
    addr_t reg_pc, reg_sp;
  } reg_file;
};

#endif // __LR35902_H
