#ifndef GBC_INSTR_HPP
#define GBC_INSTR_HPP

#include "cpu/registers/flags.hpp"
#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "memory/bus.hpp"
#include <cstddef>
#include <string>

enum class InstrStates {
  INSTR_STATE_READ,
  INSTR_STATE_WRITE,

  /**
   * For instructions which deal with 16-bit values being read and/or written to
   * memory across the span of multiple clock cycles, examples:
   *  - push
   *  - pop
   *  - call both conditional and unconditional
   *  - ret or reti
   * etc
   */
  INSTR_STATE_READ2,
  INSTR_STATE_WRITE2,
};

struct InstructionTiming {
  unsigned total_cycles;
  unsigned sync_events;
};

class Instruction {
public:
  virtual ~Instruction() = default;

  Instruction(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : reg_file(reg_file_ptr), bus(bus_ptr) {}
  /**
   * Executes the instruction in full, to be called on the memory access
   * clock cycle when appropriate.
   *
   * @return A tuple containing:
   *  - size_t: total number of clock cycles for this instruction
   */
  virtual std::size_t exec() = 0;

  /**
   * @return A tuple containing:
   *  - size_t: the memory access clock cycle of the instruction, when
   *    applicable. If memory access timing does not matter, use zero.
   */
  virtual std::size_t mem_access_t_cycle() { return 0; };

  /**
   * Parses the instruction in its entirety, reading intermediate fields
   */
  virtual InstructionTiming parse() = 0;

  /**
   * @return A string describing the instruction
   */
  virtual std::string describe() { return "Un-implemented"; }

protected:
  [[nodiscard]] static const char *to_string(Register16Bit reg) {
    switch (reg) {
    case Register16Bit::REG_AF:
      return "AF";
    case Register16Bit::REG_BC:
      return "BC";
    case Register16Bit::REG_DE:
      return "DE";
    case Register16Bit::REG_HL:
      return "HL";
    case Register16Bit::REG_SP:
      return "SP";
    }
    __builtin_unreachable();
  }

  [[nodiscard]] static const char *to_string(Register8Bit reg) {
    switch (reg) {
    case Register8Bit::REG_A:
      return "A";
    case Register8Bit::REG_F:
      return "F";
    case Register8Bit::REG_B:
      return "B";
    case Register8Bit::REG_C:
      return "C";
    case Register8Bit::REG_D:
      return "D";
    case Register8Bit::REG_E:
      return "E";
    case Register8Bit::REG_H:
      return "H";
    case Register8Bit::REG_L:
      return "L";
    }
    __builtin_unreachable();
  }

  [[nodiscard]] static const char *to_string(StatusFlagMask flag, bool expect) {
    switch (flag) {
    case StatusFlagMask::FLAG_C_MASK:
      return expect ? "C" : "!C";
    case StatusFlagMask::FLAG_N_MASK:
      return expect ? "N" : "!N";
    case StatusFlagMask::FLAG_Z_MASK:
      return expect ? "Z" : "!Z";
    case StatusFlagMask::FLAG_H_MASK:
      return expect ? "H" : "!H";
    }
    __builtin_unreachable();
  }

  template <Register16Bit reg> void write_reg(const addr_t addr) const {
    if constexpr (reg == Register16Bit::REG_AF)
      reg_file->reg_af.write(addr);
    else if constexpr (reg == Register16Bit::REG_BC)
      reg_file->reg_bc.write(addr);
    else if constexpr (reg == Register16Bit::REG_DE)
      reg_file->reg_de.write(addr);
    else if constexpr (reg == Register16Bit::REG_HL)
      reg_file->reg_hl.write(addr);
    else if constexpr (reg == Register16Bit::REG_SP)
      reg_file->reg_sp.write(addr);
    else
      static_assert("Invalid 16-bit register");
  }

  void write_reg(Register16Bit reg, const addr_t addr) const {
    switch (reg) {
    case Register16Bit::REG_AF:
      reg_file->reg_af.write(addr);
      break;
    case Register16Bit::REG_BC:
      reg_file->reg_bc.write(addr);
      break;
    case Register16Bit::REG_DE:
      reg_file->reg_de.write(addr);
      break;
    case Register16Bit::REG_HL:
      reg_file->reg_hl.write(addr);
      break;
    case Register16Bit::REG_SP:
      reg_file->reg_sp.write(addr);
      break;
    }
  }

  template <Register16Bit reg> [[nodiscard]] addr_t read_reg() const {
    if constexpr (reg == Register16Bit::REG_AF)
      return reg_file->reg_af.read();
    else if constexpr (reg == Register16Bit::REG_BC)
      return reg_file->reg_bc.read();
    else if constexpr (reg == Register16Bit::REG_DE)
      return reg_file->reg_de.read();
    else if constexpr (reg == Register16Bit::REG_HL)
      return reg_file->reg_hl.read();
    else if constexpr (reg == Register16Bit::REG_SP)
      return reg_file->reg_sp.read();
    else
      static_assert("Invalid 16-bit register");
  }

  [[nodiscard]] addr_t read_reg(Register16Bit reg) const {
    switch (reg) {
    case Register16Bit::REG_AF:
      return reg_file->reg_af.read();
    case Register16Bit::REG_BC:
      return reg_file->reg_bc.read();
    case Register16Bit::REG_DE:
      return reg_file->reg_de.read();
    case Register16Bit::REG_HL:
      return reg_file->reg_hl.read();
    case Register16Bit::REG_SP:
      return reg_file->reg_sp.read();
    }
    __builtin_unreachable();
  }

  template <Register8Bit reg> void write_reg(const byte_t byte) const {
    if constexpr (reg == Register8Bit::REG_A)
      reg_file->reg_af.write_hi(byte);
    else if constexpr (reg == Register8Bit::REG_F)
      reg_file->reg_af.write_lo(byte);
    else if constexpr (reg == Register8Bit::REG_B)
      reg_file->reg_bc.write_hi(byte);
    else if constexpr (reg == Register8Bit::REG_C)
      reg_file->reg_bc.write_lo(byte);
    else if constexpr (reg == Register8Bit::REG_D)
      reg_file->reg_de.write_hi(byte);
    else if constexpr (reg == Register8Bit::REG_E)
      reg_file->reg_de.write_lo(byte);
    else if constexpr (reg == Register8Bit::REG_H)
      reg_file->reg_hl.write_hi(byte);
    else if constexpr (reg == Register8Bit::REG_L)
      reg_file->reg_hl.write_lo(byte);
    else
      static_assert("Invalid 8-bit register");
  }

  void write_reg(Register8Bit reg, const byte_t byte) const {
    switch (reg) {
    case Register8Bit::REG_A:
      reg_file->reg_af.write_hi(byte);
      break;
    case Register8Bit::REG_F:
      reg_file->reg_af.write_lo(byte);
      break;
    case Register8Bit::REG_B:
      reg_file->reg_bc.write_hi(byte);
      break;
    case Register8Bit::REG_C:
      reg_file->reg_bc.write_lo(byte);
      break;
    case Register8Bit::REG_D:
      reg_file->reg_de.write_hi(byte);
      break;
    case Register8Bit::REG_E:
      reg_file->reg_de.write_lo(byte);
      break;
    case Register8Bit::REG_H:
      reg_file->reg_hl.write_hi(byte);
      break;
    case Register8Bit::REG_L:
      reg_file->reg_hl.write_lo(byte);
      break;
    }
  }

  template <Register8Bit reg> [[nodiscard]] byte_t read_reg() const {
    if constexpr (reg == Register8Bit::REG_A)
      return reg_file->reg_af.read_hi();
    else if constexpr (reg == Register8Bit::REG_F)
      return reg_file->reg_af.read_lo();
    else if constexpr (reg == Register8Bit::REG_B)
      return reg_file->reg_bc.read_hi();
    else if constexpr (reg == Register8Bit::REG_C)
      return reg_file->reg_bc.read_lo();
    else if constexpr (reg == Register8Bit::REG_D)
      return reg_file->reg_de.read_hi();
    else if constexpr (reg == Register8Bit::REG_E)
      return reg_file->reg_de.read_lo();
    else if constexpr (reg == Register8Bit::REG_H)
      return reg_file->reg_hl.read_hi();
    else if constexpr (reg == Register8Bit::REG_L)
      return reg_file->reg_hl.read_lo();
    else
      static_assert("Invalid 8-bit register");
    return 0xFF;
  }

  [[nodiscard]] byte_t read_reg(Register8Bit reg) const {
    switch (reg) {
    case Register8Bit::REG_A:
      return reg_file->reg_af.read_hi();
    case Register8Bit::REG_F:
      return reg_file->reg_af.read_lo();
    case Register8Bit::REG_B:
      return reg_file->reg_bc.read_hi();
    case Register8Bit::REG_C:
      return reg_file->reg_bc.read_lo();
    case Register8Bit::REG_D:
      return reg_file->reg_de.read_hi();
    case Register8Bit::REG_E:
      return reg_file->reg_de.read_lo();
    case Register8Bit::REG_H:
      return reg_file->reg_hl.read_hi();
    case Register8Bit::REG_L:
      return reg_file->reg_hl.read_lo();
    }
    __builtin_unreachable();
  }

  static inline addr_t make_addr(byte_t lo, byte_t hi) {
    return static_cast<addr_t>(lo) | (static_cast<addr_t>(hi) << 8);
  }

  RegisterFile *const reg_file;
  AddressBus *const bus;
};

#endif // GBC_INSTR_HPP
