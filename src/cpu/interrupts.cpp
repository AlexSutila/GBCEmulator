#include "cpu/interrupts.hpp"
#include <array>
#include <format>
#include <stdexcept>

using isr_metadata = std::tuple<InterruptFlagMask, InterruptVector>;

/**
 * For Interrupt Service Routines (ISR) to compute effective the call address
 * and what to set the PC to upon handling an interrupt. Do not mess with the
 * order, it is specific to the bit order in the IE/IF registers.
 */
static constexpr std::array<InterruptVector, 5> int_vector_lookup = {
    InterruptVector::INT_VECTOR_VBLANK, InterruptVector::INT_VECTOR_LCD,
    InterruptVector::INT_VECTOR_TIMER,  InterruptVector::INT_VECTOR_SERIAL,
    InterruptVector::INT_VECTOR_JOYPAD,
};

/**
 * Most unused bits read one because there is no physical hardware attached to
 * them. However, for IE, there is an exception, hence allow pulling the unused
 * bits high to be conditional.
 */
InterruptBits::InterruptBits(const bool pull_unused_high)
    : raw(0x00), pull_high(pull_unused_high) {}

void InterruptBits::write(byte_t value) {
  raw = value;
  if (pull_high)
    raw |= 0xE0;
}

byte_t InterruptBits::peek() const { return pull_high ? raw | 0xE0 : raw; }
byte_t InterruptBits::read() { return peek(); }

void InterruptBits::put_flag(InterruptFlagMask flag, bool value) {
  const byte_t mask = static_cast<byte_t>(flag);
  raw = raw & ~mask;
  if (value)
    raw = raw | mask;
}

bool InterruptBits::get_flag(InterruptFlagMask flag) const {
  const byte_t mask = static_cast<byte_t>(flag);
  return (raw & mask) != 0;
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

bool InterruptMasterEnable::is_enabled() const {
  return ime_state == IME_ENABLED;
}

/* Responsible for handling the delayed enable of the IME through `ei`. As a
 * result, this must be invoked once per instruction. */
void InterruptMasterEnable::step() {
  if (ime_state == IME_DELAYED)
    ime_state = IME_ENABLED;
}

std::string ISR::describe() { return std::format("ISR"); }

void ISR::incur_halt_delay() { halt_delay = true; }

/* See details about interrupt service routines in `interrupts.hpp` */
std::size_t ISR::exec() {
  addr_t sp = read_reg<Register16Bit::REG_SP>();

  // Consider additional four clock cycle delay when leaving halt mode
  bool was_halted = halt_delay;
  halt_delay = false;
  ime_.disable(); // Always disabled to avoid crazy recursion

  // Push old program counter onto the stack
  bus->write_byte(--sp, reg_file->reg_pc >> 8);
  bus->write_byte(--sp, reg_file->reg_pc & 0xFF);

  // Write PC and clear flag since it has been handled
  const auto [mask, vec] = calc_effective_call_addr();
  reg_file->reg_pc = static_cast<addr_t>(vec);
  if_.put_flag(mask, false);

  // Write back new value to stack pointer
  write_reg<Register16Bit::REG_SP>(sp);
  return was_halted ? 24 : 20;
}

const isr_metadata ISR::calc_effective_call_addr_ei_push() const {
  // TODO
  return {};
}

const isr_metadata ISR::calc_effective_call_addr() const {
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
