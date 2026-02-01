#include "cpu/lr35902.hpp"
#include "cpu/instr/instr.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/flags.hpp"
#include "cpu/registers/register.hpp"
#include "debugger/debugger.hpp"
#include "gbc.hpp"
#include "memory/mmio/mmio.hpp"

#include <cassert>
#include <iomanip>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

LR35902::LR35902(AddressBus *bus_ptr, std::optional<Debug::Debugger> &debugger,
                 runtime_sys_info &sys)
    : Debug::Debuggable(debugger), // For execution breakpoints on fetch
      bus(bus_ptr),                // For memory access
      sys_(sys),                   // General operating mode info
      ime(),                       // Acts as interrupt master enable
      ie_reg(false),               // Enables individual interrupts
      if_reg(true),                // Requests individual interrupts
      isr(&reg_file, bus_ptr, ime, if_reg, ie_reg) {
  using mmio = IORegisterMapping;

  /* Init fetch decode execute fsm */
  state = CpuStates::STATE_FETCH;
  total_ins_clks = std::nullopt;
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

// Lower bits get higher priority, return true if interrupted
const bool LR35902::should_interrupt() const {
  constexpr auto mask = 0x1F; // Only five interrupts
  return (ie_reg.peek() & if_reg.peek() & mask) != 0;
}

/* Read opcode from PC, populate `ins_` instruction reference */
void LR35902::do_fetch() {

  // Check for interrupts, delay fetch until after ISR
  const bool interrupted = should_interrupt();
  if (ime.is_enabled() && interrupted) {
    ins_ = &isr;
    return;
  }
  ime.step();

  // Else continue with fetch/decode/exec as usual
  const byte_t op = bus->read_byte(reg_file.reg_pc);
  std::unique_ptr<Instruction> &ins = lookup.at(op);

  // Handle un-implemented opcodes
  if (!ins) [[unlikely]] {
    std::ostringstream oss;
    oss << "Unimplemented opcode: 0x" << std::uppercase << std::hex
        << std::setw(2) << std::setfill('0') << static_cast<int>(op);
    throw std::logic_error(oss.str());

  } else
    ins_ = ins.get();

  // Save this to handle execution breakpoints
  ins_base_addr = reg_file.reg_pc;
  state = CpuStates::STATE_DECODE;

  // If the halt bug was triggered, PC freaks out and doesn't increment
  if (reg_file.halt_bug_triggered)
    reg_file.halt_bug_triggered = false;
  else reg_file.reg_pc++;
}

/* Parse operands, prepare for execution */
void LR35902::do_decode() {
  state = CpuStates::STATE_EXECUTE;
  total_ins_clks.reset();
  cur_ins_clks = 0;
  ins_->parse();

  // This must happen after `ins_->parse()` for correct operands
  try_brk(ins_base_addr, brk_reason_flags);
}

/* Execute instruction on critical mem-access clock cycle */
void LR35902::do_execute() {
  if (cur_ins_clks == ins_->mem_access_t_cycle())
    total_ins_clks = ins_->exec();
  ++cur_ins_clks;

  /* Complete instruction based on execution time */
  if (!total_ins_clks.has_value() || cur_ins_clks < total_ins_clks.value())
    return;

  /* If the instruction executed was `HALT`, the processor suspends its
   * execution until it is awaken by some interrupt source. The exact behavior
   * is conditional depending on whether IME is enabled or not. */
  else if (sys_.halted) [[unlikely]]
    state = CpuStates::STATE_HALTED;

  /* Otherwise, continue fetch/parse/execute pipeline as usual. */
  else
    state = CpuStates::STATE_FETCH;
}

void LR35902::do_halt() {
  const bool interrupted = should_interrupt();

  /* The processor waits until an interrupt is requested, in other words two
   * bits are set in IE and IF such that the bitwise AND is non-zero. The
   * behavior varies when IME is enabled or disabled. */
  if (!interrupted)
    return;

  // Leave halt mode when an interrupt is pending
  sys_.halted = false;

  /* If IME is enabled, execution stops until the interrupt is requested, then
   * interrupt is serviced and execution resumes as normal. */
  if (ime.is_enabled()) {
    isr.incur_halt_delay();
    ins_ = &isr;
    state = CpuStates::STATE_DECODE;
  }

  /* If IME is disabled, the execution still stops. The only difference is the
   * interrupt will not be serviced and it just continues executing from the
   * instruction following `HALT`. */
  else {
    state = CpuStates::STATE_FETCH;
  }
}

void LR35902::step() {
  switch (state) {
  case STATE_FETCH: // Break omitted intentionally
    do_fetch();
  case STATE_DECODE: // Break omitted intentionally
    do_decode();
  case STATE_EXECUTE:
    do_execute();
    break;
  case STATE_HALTED:
    do_halt();
    break;
  }
}
