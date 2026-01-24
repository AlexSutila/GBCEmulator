#include "cpu/interrupts.hpp"

/* Most unused bits read one because there is no physical hardware attached to
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
    ime_state = IME_PENDING;
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
  if (ime_state == IME_PENDING)
    ime_state = IME_DELAYED;
  else if (ime_state == IME_DELAYED)
    ime_state = IME_ENABLED;
}
