#include "cpu/lr35902.hpp"
#include "cpu/instr/instr.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/register.hpp"
#include "memory/mmio.hpp"

#include <cassert>
#include <iomanip>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

LR35902::LR35902(AddressBus *bus_ptr) : bus(bus_ptr) {
  using ioregs = IORegisterMapping;
  using flags = InterruptFlagMask;
  using vecs = InterruptVector;

  /* Init fetch decode execute fsm */
  state = CpuStates::STATE_FETCH;
  total_ins_clks = std::nullopt;

  /* Register initialization */
  reg_file.reg_af = CpuFlagsRegister();
  reg_file.reg_bc = CpuRegister();
  reg_file.reg_de = CpuRegister();
  reg_file.reg_hl = CpuRegister();
  reg_file.reg_sp = CpuRegister();
  reg_file.reg_pc = 0x0000;
  lookup = {};

  /* Configure opcode lookup tables */
  init_alu(lookup);
  init_bitops(lookup);
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

  /* Lastly, configure interrupt service routines */
  isr_lookup = {
      mk_isr<flags::INT_FLAG_VBLANK, vecs::INT_VECTOR_VBLANK>(),
      mk_isr<flags::INT_FLAG_LCD, vecs::INT_VECTOR_LCD>(),
      mk_isr<flags::INT_FLAG_TIMER, vecs::INT_VECTOR_TIMER>(),
      mk_isr<flags::INT_FLAG_SERIAL, vecs::INT_VECTOR_SERIAL>(),
      mk_isr<flags::INT_FLAG_JOYPAD, vecs::INT_VECTOR_JOYPAD>(),
  };
}

template <InterruptFlagMask mask, InterruptVector vec>
std::unique_ptr<Instruction> LR35902::mk_isr() {
  return std::make_unique<ISR<mask, vec>>(&reg_file, bus, &ime, if_reg);
}

void LR35902::load_state(LR35902::ProcessorState state) {
  reg_file.reg_pc = state.pc;
  reg_file.reg_sp.write(state.sp);
  reg_file.reg_af.write_hi(state.a);
  reg_file.reg_af.write_lo(state.f);
  reg_file.reg_bc.write_hi(state.b);
  reg_file.reg_bc.write_lo(state.c);
  reg_file.reg_de.write_hi(state.d);
  reg_file.reg_de.write_lo(state.e);
  reg_file.reg_hl.write_hi(state.h);
  reg_file.reg_hl.write_lo(state.l);

  /* Do not delay IME enable */
  if (state.ime_enabled)
    ime.enable(false);
  else
    ime.disable();
}

LR35902::ProcessorState LR35902::get_state() const {
  ProcessorState state{};
  state.pc = reg_file.reg_pc;
  state.sp = reg_file.reg_sp.read();
  state.a = reg_file.reg_af.read_hi();
  state.f = reg_file.reg_af.read_lo();
  state.b = reg_file.reg_bc.read_hi();
  state.c = reg_file.reg_bc.read_lo();
  state.d = reg_file.reg_de.read_hi();
  state.e = reg_file.reg_de.read_lo();
  state.h = reg_file.reg_hl.read_hi();
  state.l = reg_file.reg_hl.read_lo();

  /* Snapshot effective IME state only */
  state.ime_enabled = ime.is_enabled();
  return state;
}

std::tuple<bool, Instruction *> LR35902::should_interrupt() {
  if (!ime.is_enabled())
    return {false, nullptr};

  // Lower bits get higher priority, return the corresponding ISR
  for (byte_t shift{0}; shift < 5; shift++) {
    const auto flag = static_cast<InterruptFlagMask>(1 << shift);
    if (ie_reg->get_flag(flag) && if_reg->get_flag(flag))
      return {true, isr_lookup.at(shift).get()};
  }
  return {false, nullptr};
}

/* Read opcode from PC, populate `ins_` instruction reference */
void LR35902::fetch() {
  ime.step();

  // Check for interrupts, delay fetch until after ISR
  auto [interrupted, isr] = should_interrupt();
  if (interrupted)
    ins_ = isr;

  // Else continue with fetch/decode/exec as usual
  else {
    const byte_t op = bus->read_byte(reg_file.reg_pc++);
    std::unique_ptr<Instruction> &i = lookup.at(op);

    // Handle un-implemented opcodes
    if (!i) [[unlikely]] {
      std::ostringstream oss;
      oss << "Unimplemented opcode: 0x" << std::uppercase << std::hex
          << std::setw(2) << std::setfill('0') << static_cast<int>(op);
      throw std::logic_error(oss.str());
    } else
      ins_ = i.get();
  }
  state = CpuStates::STATE_DECODE;
}

/* Parse operands, prepare for execution */
void LR35902::decode() {
  state = CpuStates::STATE_EXECUTE;
  total_ins_clks.reset();
  cur_ins_clks = 0;
  ins_->parse();
}

/* Execute instruction on critical mem-access clock cycle */
void LR35902::execute() {
  if (cur_ins_clks == ins_->mem_access_t_cycle())
    total_ins_clks = ins_->exec();
  ++cur_ins_clks;

  /* Complete instruction based on execution time */
  if (total_ins_clks.has_value() && cur_ins_clks == total_ins_clks.value())
    state = CpuStates::STATE_FETCH;
}

void LR35902::step() {
  switch (state) {
  case STATE_FETCH: // Break omitted intentionally
    fetch();
  case STATE_DECODE: // Break omitted intentionally
    decode();
  case STATE_EXECUTE:
    execute();
    break;
  }
}
