#include <cpu/interrupts.hpp>

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

byte_t InterruptBits::read() {
  if (pull_high)
    raw |= 0xE0;
  return raw;
}

InterruptMasterEnable::InterruptMasterEnable()
    : ime_pending(false), ime_true(false) {}

/* If enabled via `ei`, the IME is not actually enabled until one instruction
 * later. If enabled via `reti`, the effects of enabling the IME occur
 * instantly. */
void InterruptMasterEnable::enable(bool delayed) {
  if (delayed)
    ime_pending = true;
  else
    ime_true = true;
}

/* Under no circumstance are IME disables delayed. The effects of the `di`
 * instruction always occur immediately. */
void InterruptMasterEnable::disable() {
  ime_pending = false;
  ime_true = false;
}

bool InterruptMasterEnable::is_enabled() const { return ime_true; }

/* Responsible for handling the delayed enable of the IME through `ei`. As a
 * result, this must be invoked once per instruction. */
void InterruptMasterEnable::step() {
  if (!ime_pending)
    return;
  ime_pending = false;
  ime_true = true;
}
