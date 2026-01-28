#ifndef __REGISTER_FILE_H
#define __REGISTER_FILE_H

#include "cpu/registers/flags.hpp"
#include "cpu/registers/register.hpp"

/*
 * LR35902 Register Set is as follows, where each register is
 * sixteen bits. Registers can be used as either full sixteen
 * bit registers, or two eight bit registers.
 *
 * 16-bit | Hi | Lo | Name / Function
 * -------+----+----+-------------------------
 * AF     | A  | -  | Accumulator & Flags
 * BC     | B  | C  | BC
 * DE     | D  | E  | DE
 * HL     | H  | L  | HL
 * SP     | -  | -  | Stack Pointer
 * PC     | -  | -  | Program Counter / Pointer
 */
struct RegisterFile {
  CpuRegister reg_bc, reg_de, reg_hl, reg_sp;
  CpuFlagsRegister reg_af;
  addr_t reg_pc; // Instruction pointer

  // TODO: Consider EI and RST quirks
  bool halt_bug_triggered{false};
};

// Used for compile time register decoding
enum class Register8Bit {
  REG_A,
  REG_F,
  REG_B,
  REG_C,
  REG_D,
  REG_E,
  REG_H,
  REG_L,
};

// Used for compile time register decoding
enum class Register16Bit {
  REG_AF,
  REG_BC,
  REG_DE,
  REG_HL,
  REG_SP,
};

#endif // __REGISTER_FILE_H
