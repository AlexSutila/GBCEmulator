#ifndef __MOVES_H
#define __MOVES_H

#include <cpu/instr/instr.hpp>
#include <cpu/lr35902.hpp>
#include <emu_types.hpp>
#include <memory/bus.hpp>

/*
 * Copies value from Y into X, operates only on 8-bit registers
 */
template <Register8Bit src, Register8Bit dst>
class LD_X_Y : public Instruction {
public:
  LD_X_Y(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<bool, std::size_t> step() override {
    const byte_t val = read_reg<src>();
    write_reg<dst>(val);
    return {true, 4};
  }
  addr_t cycles() const override { return 4; }
};

/*
 * Copies immediate value into X, operates only on 8-bit registers
 */
template <Register8Bit dst> class LD_X_imm : public Instruction {
public:
  LD_X_imm(RegisterFile *reg_file_ptr, AddressBus *bus_ptr)
      : Instruction(reg_file_ptr, bus_ptr) {}
  std::tuple<bool, std::size_t> step() override {
    write_reg<dst>(imm);
    return {true, 8};
  }
  void parse() override {
    const addr_t addr = reg_file->reg_pc++;
    imm = bus->read_byte(addr);
  }
  addr_t cycles() const override { return 8; }

private:
  byte_t imm;
};

#endif // __MOVES_H
