#ifndef __LR35902_H
#define __LR35902_H

#include <cpu/registers/flags.hpp>
#include <cpu/registers/register.hpp>
#include <emu_types.hpp>
#include <memory/bus.hpp>

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
  CpuRegister reg_bc, reg_de, reg_hl;
  CpuFlagsRegister reg_af;
  addr_t reg_pc, reg_sp;
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
};

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
};

#endif // __LR35902_H
