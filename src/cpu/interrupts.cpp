#include "cpu/interrupts.hpp"
#include "utils.hpp"

#include <array>
#include <stdexcept>

using isr_metadata = std::tuple<InterruptFlagMask, InterruptVector>;

/**
 * For Interrupt Service Routines (ISR) to compute effective the call address
 * and what to set the PC to upon handling an interrupt. Do not mess with the
 * order, it is specific to the bit order in the IE/IF registers.
 */
static constexpr std::array int_vector_lookup = {
    InterruptVector::INT_VECTOR_VBLANK, InterruptVector::INT_VECTOR_LCD,
    InterruptVector::INT_VECTOR_TIMER,  InterruptVector::INT_VECTOR_SERIAL,
    InterruptVector::INT_VECTOR_JOYPAD,
};

/**
 * Most unused bits read one because there is no physical hardware attached to
 * them. However, for IE, there is an exception, hence allow pulling the unused
 * bits high to be conditional.
 */
InterruptBits::InterruptBits(const bool pull_unused_high) : pull_high(pull_unused_high) {}

void InterruptBits::write(const byte_t value) {
  state_ = value;
  if (pull_high)
    state_ |= 0xE0;
}

byte_t InterruptBits::peek() const { return pull_high ? state_ | 0xE0 : state_; }
byte_t InterruptBits::read() { return peek(); }

void InterruptBits::put_flag(InterruptFlagMask flag, const bool value) {
  const auto mask = static_cast<byte_t>(flag);
  state_ = state_ & ~mask;
  if (value)
    state_ = state_ | mask;
}

bool InterruptBits::get_flag(InterruptFlagMask flag) const {
  const auto mask = static_cast<byte_t>(flag);
  return (state_ & mask) != 0;
}

InterruptMasterEnable::InterruptMasterEnable() : ime_state(IME_DISABLED) {}

/* If enabled via `ei`, the IME is not actually enabled until one instruction
 * later. If enabled via `reti`, the effects of enabling the IME occur
 * instantly. */
void InterruptMasterEnable::enable(bool delayed) {
  if (delayed && ime_state != IME_ENABLED)
    ime_state = IME_DELAYED;
  else
    ime_state = IME_ENABLED;
}

/* Under no circumstance are IME disables delayed. The effects of the `di`
 * instruction always occur immediately. */
void InterruptMasterEnable::disable() { ime_state = IME_DISABLED; }

bool InterruptMasterEnable::is_enabled() const { return ime_state == IME_ENABLED; }

byte_t InterruptMasterEnable::raw_state() const { return static_cast<byte_t>(ime_state); }

void InterruptMasterEnable::load_raw_state(const byte_t state) {
  switch (state) {
  case IME_DELAYED:
  case IME_ENABLED:
  case IME_DISABLED:
    ime_state = static_cast<ImeStates>(state);
    return;
  default:
    throw std::runtime_error("InterruptMasterEnable::load_raw_state()");
  }
}

/* Responsible for handling the delayed enable of the IME through `ei`. As a
 * result, this must be invoked once per instruction. */
void InterruptMasterEnable::step() {
  if (ime_state == IME_DELAYED)
    ime_state = IME_ENABLED;
}

std::string ISR::describe() { return IroGB::format("ISR"); }

void ISR::incur_halt_delay() { halt_delay = true; }

/* See details about interrupt service routines in `interrupts.hpp` */
std::size_t ISR::exec() {
  const auto [flag, vec] = calc_effective_call_addr();
  const addr_t sp = read_reg<Register16Bit::REG_SP>();

  // Consider additional four clock cycle delay when leaving halt mode
  const bool was_halted = halt_delay;
  halt_delay = false;
  ime_.disable(); // Always disabled to avoid crazy recursion

  // Push old program counter onto the stack
  const byte_t pc_hi = reg_file->reg_pc >> 8;
  const byte_t pc_lo = reg_file->reg_pc & 0xFF;
  bus->write_byte(sp - 1, pc_hi);
  bus->write_byte(sp - 2, pc_lo);

  // Handle strange behavior when low byte of upper byte push overwrites IE
  if (sp == 0 && (static_cast<byte_t>(flag) & pc_hi) == 0) [[unlikely]]
    handle_ei_push_bug();

  // Expected behavior
  else [[likely]] {
    reg_file->reg_pc = static_cast<addr_t>(vec);
    if_.put_flag(flag, false);
  }
  write_reg<Register16Bit::REG_SP>(sp - 2);
  return was_halted ? 24 : 20;
}

void ISR::handle_ei_push_bug() const {
  constexpr auto mask = 0x1F;
  constexpr addr_t pc_bugged = 0;

  // Case 1: The simple scenario is, the EI overwrite disabled interrupts, and
  // as a result the PC freaks out and returns zero. No flags are cleared.
  if ((ie_.peek() & if_.peek() & mask) == 0)
    reg_file->reg_pc = pc_bugged;

  // Case 2: The EI overwrite messed the pending interrupt up, but another one
  // is still pending and the corresponding EI bit is still set. This interrupt
  // will be served normally.
  else {
    const auto [flag, vec] = calc_effective_call_addr();
    reg_file->reg_pc = static_cast<addr_t>(vec);
    if_.put_flag(flag, false);
  }
}

isr_metadata ISR::calc_effective_call_addr() const {
  constexpr auto num_interrupts = 5;

  // Lower bits have higher priority, so check them first
  for (byte_t shift{0}; shift < num_interrupts; shift++) {
    const auto flag = static_cast<InterruptFlagMask>(1 << shift);
    if (ie_.get_flag(flag) && if_.get_flag(flag))
      return {flag, int_vector_lookup.at(shift)};
  }

  // We make an assumption that if this has been, an interrupt is in progress.
  throw std::runtime_error("ISR::calc_effective_call_addr_ei_push()");
}
