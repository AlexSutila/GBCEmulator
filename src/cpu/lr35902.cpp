#include "cpu/lr35902.hpp"
#include "cpu/instr/instr.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/register.hpp"
#include "debugger/debugger.hpp"
#include "gbc.hpp"
#include "memory/mmio/mmio.hpp"
#include "savestate/codec.hpp"

#include <iomanip>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

/**
 * Note that even though we force alignment of savestates with CPU instruction fetches,
 * we still have to save the stateful information to know if we are in halt mode or not.
 */
enum : std::uint16_t {
  F_PC = 1,
  F_SP,
  F_A,
  F_B,
  F_C,
  F_D,
  F_E,
  F_F,
  F_H,
  F_L,
  F_IME_RAW,
  F_HALT_BUG,
  F_INS_BASE,
  F_STATE,

  // CPU owns these registers (presumably), so we parse them here
  F_IF_FLAGS,
  F_IE_FLAGS,
};

template <typename T> void LR35902::parse_savestate(T &t) {
  constexpr auto version = 1; // Schema revision
  t.chunk_header(version, Savestate::C_CPU);

  ProcessorState cpu_state{};
  if (t.op() == Savestate::OP_WRITE)
    cpu_state = get_state();

  // Exploiting public API exposed to pybindings here
  t.field_generic(F_PC, cpu_state.pc);
  t.field_generic(F_SP, cpu_state.sp);
  t.field_generic(F_A, cpu_state.a);
  t.field_generic(F_B, cpu_state.b);
  t.field_generic(F_C, cpu_state.c);
  t.field_generic(F_D, cpu_state.d);
  t.field_generic(F_E, cpu_state.e);
  t.field_generic(F_F, cpu_state.f);
  t.field_generic(F_H, cpu_state.h);
  t.field_generic(F_L, cpu_state.l);
  t.field_generic(F_IME_RAW, cpu_state.ime_enabled);

  if (t.op() == Savestate::OP_READ)
    load_state(cpu_state);

  // These are not manipulated by the data exposed via public API
  t.field_generic(F_HALT_BUG, reg_file.halt_bug_triggered);
  t.field_generic(F_INS_BASE, ins_base_addr);
  t.field_enum(F_STATE, state);

  // Memory mapped registers for interrupts
  t.field_complex(F_IF_FLAGS, [&](T &t) { if_reg.parse_savestate(t); });
  t.field_complex(F_IE_FLAGS, [&](T &t) { ie_reg.parse_savestate(t); });
  t.eof();
}

template void LR35902::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void LR35902::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void LR35902::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void LR35902::parse_savestate<Savestate::Checker>(Savestate::Checker &);

LR35902::LR35902(AddressBus *bus_ptr, std::optional<Debug::Debugger> &debugger,
                 runtime_sys_info &sys)
    : Debuggable(debugger), // For execution breakpoints on fetch
      bus(bus_ptr),         // For memory access
      sys_(sys),            // Acts as interrupt master enable
      ie_reg(false),        // Enables individual interrupts
      if_reg(true),         // Requests individual interrupts
      isr(&reg_file, bus_ptr, ime, if_reg, ie_reg) {
  using mmio = IORegisterMapping;

  /* Init fetch decode execute fsm */
  state = STATE_FETCH;
  cur_ins_clks = 0;

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
  init_control(lookup, sys);
  init_moves(lookup);

  /* Configure interrupts */
  if (!bus)
    throw std::logic_error("LR35902::LR35902() bus_ptr is `nullptr`");
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_INT_FLAGS), &if_reg);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_INT_ENABLE), &ie_reg);
}

void LR35902::load_state(const ProcessorState state_) {
  reg_file.reg_pc = state_.pc;
  reg_file.reg_sp.write(state_.sp);
  reg_file.reg_af.write_hi(state_.a);
  reg_file.reg_af.write_lo(state_.f);
  reg_file.reg_bc.write_hi(state_.b);
  reg_file.reg_bc.write_lo(state_.c);
  reg_file.reg_de.write_hi(state_.d);
  reg_file.reg_de.write_lo(state_.e);
  reg_file.reg_hl.write_hi(state_.h);
  reg_file.reg_hl.write_lo(state_.l);

  /* Do not delay IME enable */
  if (state_.ime_enabled)
    ime.enable(false);
  else
    ime.disable();
}

LR35902::ProcessorState LR35902::get_state() const {
  ProcessorState state_{};
  state_.pc = reg_file.reg_pc;
  state_.sp = reg_file.reg_sp.read();
  state_.a = reg_file.reg_af.read_hi();
  state_.f = reg_file.reg_af.read_lo();
  state_.b = reg_file.reg_bc.read_hi();
  state_.c = reg_file.reg_bc.read_lo();
  state_.d = reg_file.reg_de.read_hi();
  state_.e = reg_file.reg_de.read_lo();
  state_.h = reg_file.reg_hl.read_hi();
  state_.l = reg_file.reg_hl.read_lo();

  /* Snapshot effective IME state only */
  state_.ime_enabled = ime.is_enabled();
  return state_;
}

bool LR35902::savestate_ready() const { return state == STATE_FETCH || state == STATE_HALTED; }

// Lower bits get higher priority, return true if interrupted
bool LR35902::should_interrupt() const {
  constexpr auto mask = 0x1F; // Only five interrupts
  return (ie_reg.peek() & if_reg.peek() & mask) != 0;
}

void LR35902::do_exec_state_transition() {
  /* If the instruction executed was `HALT`, the processor suspends its
   * execution until it is awakened by some interrupt source. The exact behavior
   * is conditional depending on whether IME is enabled or not. */
  if (sys_.halted) [[unlikely]]
    state = STATE_HALTED;

  /* Otherwise, continue fetch/parse/execute pipeline as usual. */
  else
    state = STATE_FETCH;
}

/* Read opcode from PC, populate `ins_` instruction reference */
void LR35902::do_fetch() {

  // Check for interrupts, delay fetch until after ISR
  const bool interrupted = should_interrupt();
  if (ime.is_enabled() && interrupted) {
    prime_next_instr(&isr);
    return;
  }
  ime.step();

  // Else continue with fetch/decode/exec as usual
  const byte_t op = bus->read_byte(reg_file.reg_pc);
  const std::unique_ptr<Instruction> &next_ins = lookup.at(op);

  // Handle un-implemented opcodes
  if (!next_ins) [[unlikely]] {
    std::ostringstream oss;
    oss << "Unimplemented opcode: 0x" << std::uppercase << std::hex << std::setw(2)
        << std::setfill('0') << static_cast<int>(op);
    throw std::logic_error(oss.str());
  }

  // Save this to handle execution breakpoints
  ins_base_addr = reg_file.reg_pc;

  // If the halt bug was triggered, PC freaks out and doesn't increment
  if (reg_file.halt_bug_triggered)
    reg_file.halt_bug_triggered = false;
  else
    reg_file.reg_pc++;

  // Prepare next instruction for execution stage
  prime_next_instr(next_ins.get());
}

void LR35902::do_halt() {
  /* The processor waits until an interrupt is requested, in other words two
   * bits are set in IE and IF such that the bitwise AND is non-zero. The
   * behavior varies when IME is enabled or disabled. */
  if (const bool interrupted = should_interrupt(); !interrupted)
    return;

  // Leave halt mode when an interrupt is pending
  sys_.halted = false;

  /* If IME is enabled, execution stops until the interrupt is requested, then
   * interrupt is serviced and execution resumes as normal. */
  if (ime.is_enabled()) {
    isr.incur_halt_delay();
    prime_next_instr(&isr);
  }

  /* If IME is disabled, the execution still stops. The only difference is the
   * interrupt will not be serviced, and it just continues executing from the
   * instruction following `HALT`. */
  else {
    state = STATE_FETCH;
  }
}

void LR35902::prime_next_instr(Instruction *const next_ins) {
  state = STATE_EXECUTE;
  ins_ = next_ins;

  timing_info = ins_->parse();
  cur_ins_clks = 0;

  // This must happen after `ins_->parse()` for correct operands
  try_brk(sys_.elapsed_clocks, ins_base_addr, brk_reason_flags);
}

void LR35902::step() {
  switch (state) {
  case STATE_FETCH: // Break omitted intentionally to emulate fetch/exec overlap
    do_fetch();

  case STATE_EXECUTE: {
    if (cur_ins_clks == ins_->next_sync_cycle())
      ins_->exec();
    ++cur_ins_clks;

    /* Complete instruction based on execution time */
    if (cur_ins_clks >= timing_info.total_cycles)
      do_exec_state_transition();
  } break;

  case STATE_HALTED:
    do_halt();
    break;
  }
}
