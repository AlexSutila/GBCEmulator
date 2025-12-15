#ifndef __LR35902_H
#define __LR35902_H

#include <array>
#include <cpu/instr/instr.hpp>
#include <cpu/registers/regfile.hpp>
#include <emu_types.hpp>
#include <memory/bus.hpp>
#include <memory>

/*
 * 8-bit 8080-like Sharp CPU (speculated to be a SM83 core), running
 * between 4.194304 MHz and 8.388608 MHz based on mode of operation
 */
class LR35902 {
public:
  LR35902(AddressBus *bus_ptr);
  void step();

private:
  AddressBus *const bus;
  RegisterFile reg_file;

  /* Opcode decoding configuration */
  using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
  void init_moves(lookup_table_t &lookup);
  lookup_table_t lookup;
};

#endif // __LR35902_H
