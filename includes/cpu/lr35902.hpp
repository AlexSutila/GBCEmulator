#ifndef __LR35902_H
#define __LR35902_H

#include "cpu/instr/instr.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/regfile.hpp"
#include "memory/bus.hpp"

#include <array>
#include <memory>

/*
 * 8-bit 8080-like Sharp CPU (speculated to be a SM83 core), running
 * between 4.194304 MHz and 8.388608 MHz based on mode of operation
 */
class LR35902 {
public:
  LR35902(AddressBus *bus_ptr);
  void step();

  struct ProcessorState {
    addr_t pc;
    addr_t sp;
    byte_t a;
    byte_t b;
    byte_t c;
    byte_t d;
    byte_t e;
    byte_t f;
    byte_t h;
    byte_t l;
    bool ime_enabled;
  };
  void load_state(ProcessorState state);
  ProcessorState get_state() const;
  std::size_t get_clocks() const { return clocks_elapsed; };

private:
  RegisterFile reg_file{};
  AddressBus *const bus{};

  /* Interrupt handling */
  InterruptMasterEnable ime{};
  InterruptBits *ie_reg{};
  InterruptBits *if_reg{};

  /* Opcode decoding configuration */
  using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
  void init_alu(lookup_table_t &lookup);
  void init_bitops(lookup_table_t &lookup);
  void init_branch(lookup_table_t &lookup);
  void init_control(lookup_table_t &lookup);
  void init_moves(lookup_table_t &lookup);
  lookup_table_t lookup{};

  /* Timing metadata */
  std::size_t clocks_elapsed{};
};

#endif // __LR35902_H
