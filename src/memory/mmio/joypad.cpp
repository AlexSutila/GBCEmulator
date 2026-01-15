#include "memory/mmio/dmg.hpp"
#include "cpu/interrupts.hpp"

constexpr byte_t select_mask = 0x30;
constexpr byte_t high_bits = 0xC0;

namespace Joypad {

JOYP::JOYP() : select_bits(select_mask), button_state(0), last_low(0x0F) {}

void JOYP::set_interrupt_reg(InterruptBits *reg) { if_reg = reg; }

void JOYP::set_button(JoypadButton button, const bool pressed) {
  const byte_t mask = static_cast<byte_t>(button);
  if (pressed)
    button_state |= mask;
  else
    button_state &= static_cast<byte_t>(~mask);
  update_output(compute_low_bits());
}

void JOYP::set_state(byte_t mask) {
  button_state = mask;
  update_output(compute_low_bits());
}

void JOYP::write(byte_t value) {
  select_bits = value & select_mask;
  update_output(compute_low_bits());
}

byte_t JOYP::read() {
  const byte_t low = compute_low_bits();
  return static_cast<byte_t>(high_bits | select_bits | low);
}

byte_t JOYP::compute_low_bits() const {
  byte_t low = 0x0F;

  if ((select_bits & 0x10) == 0) {
    byte_t dir = 0x0F;
    if (button_state & static_cast<byte_t>(JoypadButton::RIGHT))
      dir &= static_cast<byte_t>(~0x01);
    if (button_state & static_cast<byte_t>(JoypadButton::LEFT))
      dir &= static_cast<byte_t>(~0x02);
    if (button_state & static_cast<byte_t>(JoypadButton::UP))
      dir &= static_cast<byte_t>(~0x04);
    if (button_state & static_cast<byte_t>(JoypadButton::DOWN))
      dir &= static_cast<byte_t>(~0x08);
    low &= dir;
  }

  if ((select_bits & 0x20) == 0) {
    byte_t action = 0x0F;
    if (button_state & static_cast<byte_t>(JoypadButton::A))
      action &= static_cast<byte_t>(~0x01);
    if (button_state & static_cast<byte_t>(JoypadButton::B))
      action &= static_cast<byte_t>(~0x02);
    if (button_state & static_cast<byte_t>(JoypadButton::SELECT))
      action &= static_cast<byte_t>(~0x04);
    if (button_state & static_cast<byte_t>(JoypadButton::START))
      action &= static_cast<byte_t>(~0x08);
    low &= action;
  }

  return low;
}

void JOYP::update_output(const byte_t next_low) {
  if (if_reg) {
    const byte_t pressed = static_cast<byte_t>(last_low & ~next_low);
    if (pressed != 0)
      if_reg->put_flag(InterruptFlagMask::INT_FLAG_JOYPAD, true);
  }
  last_low = next_low;
}

} // namespace Joypad
