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

LR35902::LR35902(AddressBus *bus_ptr, std::optional<Debug::Debugger> &debugger,
                 runtime_sys_info &sys)
    : Debuggable(debugger), // For execution breakpoints on fetch
      bus(bus_ptr),                // For memory access
      sys_(sys),                       // Acts as interrupt master enable
      ie_reg(false),               // Enables individual interrupts
      if_reg(true),                // Requests individual interrupts
      isr(&reg_file, bus_ptr, ime, if_reg, ie_reg) {
  using mmio = IORegisterMapping;

  /* Init fetch decode execute fsm */
  state = STATE_FETCH;
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
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_INT_FLAGS), &if_reg,
                    MMIOSavestatePolicy::BusAuto);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_INT_ENABLE), &ie_reg,
                    MMIOSavestatePolicy::BusAuto);
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

bool LR35902::savestate_ready() const {
  return state == STATE_FETCH || state == STATE_HALTED;
}

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
  F_CPU_STATE,
  F_INS_BASE,
};

void LR35902::savestate_serialize(Savestate::Writer &out) const {
  if (!savestate_ready())
    throw std::runtime_error("LR35902::savestate_serialize() not at boundary");
  const auto regs = get_state();
  out.field_u16(F_PC, regs.pc);
  out.field_u16(F_SP, regs.sp);
  out.field_u8(F_A, regs.a);
  out.field_u8(F_B, regs.b);
  out.field_u8(F_C, regs.c);
  out.field_u8(F_D, regs.d);
  out.field_u8(F_E, regs.e);
  out.field_u8(F_F, regs.f);
  out.field_u8(F_H, regs.h);
  out.field_u8(F_L, regs.l);
  out.field_u8(F_IME_RAW, ime.raw_state());
  out.field_bool(F_HALT_BUG, reg_file.halt_bug_triggered);
  out.field_u8(F_CPU_STATE, static_cast<byte_t>(state));
  out.field_u16(F_INS_BASE, ins_base_addr);
}

void LR35902::savestate_deserialize(Savestate::Reader &in) {
  ProcessorState regs = get_state();
  byte_t ime_state = ime.raw_state();
  reg_file.halt_bug_triggered = false;
  auto cpu_state = static_cast<byte_t>(state);

  while (const auto field = in.next_field()) {
    auto [id, payload] = *field;
    switch (id) {
    case F_PC:
      regs.pc = payload.u16();
      break;
    case F_SP:
      regs.sp = payload.u16();
      break;
    case F_A:
      regs.a = payload.u8();
      break;
    case F_B:
      regs.b = payload.u8();
      break;
    case F_C:
      regs.c = payload.u8();
      break;
    case F_D:
      regs.d = payload.u8();
      break;
    case F_E:
      regs.e = payload.u8();
      break;
    case F_F:
      regs.f = payload.u8();
      break;
    case F_H:
      regs.h = payload.u8();
      break;
    case F_L:
      regs.l = payload.u8();
      break;
    case F_IME_RAW:
      ime_state = payload.u8();
      break;
    case F_HALT_BUG:
      reg_file.halt_bug_triggered = payload.boolean();
      break;
    case F_CPU_STATE:
      cpu_state = payload.u8();
      break;
    case F_INS_BASE:
      ins_base_addr = payload.u16();
      break;
    default:
      payload.skip(payload.remaining());
      break;
    }
    payload.expect_eof();
  }

  if (cpu_state != STATE_FETCH && cpu_state != STATE_HALTED)
    throw std::runtime_error(
        "LR35902::savestate_deserialize() invalid pipeline state");

  regs.ime_enabled = false;
  load_state(regs);
  ime.load_raw_state(ime_state);

  state = static_cast<CpuStates>(cpu_state);
  ins_ = nullptr;
  total_ins_clks.reset();
  cur_ins_clks = 0;
}

// Lower bits get higher priority, return true if interrupted
bool LR35902::should_interrupt() const {
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
  const std::unique_ptr<Instruction> &ins = lookup.at(op);

  // Handle un-implemented opcodes
  if (!ins) [[unlikely]] {
    std::ostringstream oss;
    oss << "Unimplemented opcode: 0x" << std::uppercase << std::hex
        << std::setw(2) << std::setfill('0') << static_cast<int>(op);
    throw std::logic_error(oss.str());
  }
  ins_ = ins.get();

  // Save this to handle execution breakpoints
  ins_base_addr = reg_file.reg_pc;
  state = STATE_DECODE;

  // If the halt bug was triggered, PC freaks out and doesn't increment
  if (reg_file.halt_bug_triggered)
    reg_file.halt_bug_triggered = false;
  else reg_file.reg_pc++;
}

/* Parse operands, prepare for execution */
void LR35902::do_decode() {
  state = STATE_EXECUTE;
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
   * execution until it is awakened by some interrupt source. The exact behavior
   * is conditional depending on whether IME is enabled or not. */
  if (sys_.halted) [[unlikely]]
    state = STATE_HALTED;

  /* Otherwise, continue fetch/parse/execute pipeline as usual. */
  else
    state = STATE_FETCH;
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
    ins_ = &isr;
    state = STATE_DECODE;
  }

  /* If IME is disabled, the execution still stops. The only difference is the
   * interrupt will not be serviced, and it just continues executing from the
   * instruction following `HALT`. */
  else {
    state = STATE_FETCH;
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
