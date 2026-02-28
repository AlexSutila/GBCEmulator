#include "cpu/interrupts.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "savestate/codec.hpp"

constexpr byte_t select_mask = 0x30;
constexpr byte_t high_bits = 0xC0;

namespace Joypad {

JOYP::JOYP() : MMIORegister(0), select_bits(select_mask), last_low(0x0F) {}

void JOYP::set_interrupt_reg(InterruptBits *reg) { if_reg = reg; }

JOYP::SavestateState JOYP::savestate_get() const {
  return {
      .buttons = raw_state(),
      .select = select_bits,
      .last_low = last_low,
  };
}

void JOYP::savestate_load(const SavestateState &snapshot) {
  raw_state_set(snapshot.buttons);
  select_bits = snapshot.select & select_mask;
  last_low = snapshot.last_low & 0x0F;
}

void JOYP::savestate_serialize(Savestate::Writer &out) const {
  const auto [buttons, select, last_low_state] = savestate_get();
  out.field_u8(1, buttons);
  out.field_u8(2, select);
  out.field_u8(3, last_low_state);
}

void JOYP::savestate_deserialize(Savestate::Reader &in) {
  SavestateState snapshot = savestate_get();
  while (const auto field = in.next_field()) {
    auto [id, payload] = *field;
    switch (id) {
    case 1:
      snapshot.buttons = payload.u8();
      break;
    case 2:
      snapshot.select = payload.u8();
      break;
    case 3:
      snapshot.last_low = payload.u8();
      break;
    default:
      payload.skip(payload.remaining());
      break;
    }
    payload.expect_eof();
  }
  savestate_load(snapshot);
}

void JOYP::set_button(JoypadButton button, const bool pressed) {
  const auto mask = static_cast<byte_t>(button);
  if (pressed)
    raw_state() |= mask;
  else
    raw_state() &= static_cast<byte_t>(~mask);
  update_output(compute_low_bits());
}

void JOYP::set_state(const byte_t mask) {
  raw_state_set(mask);
  update_output(compute_low_bits());
}

void JOYP::write(const byte_t value) {
  select_bits = value & select_mask;
  update_output(compute_low_bits());
}

byte_t JOYP::peek() const {
  const byte_t low = compute_low_bits();
  return static_cast<byte_t>(high_bits | select_bits | low);
}

byte_t JOYP::read() { return peek(); }

byte_t JOYP::compute_low_bits() const {
  byte_t low = 0x0F;

  if ((select_bits & 0x10) == 0) {
    byte_t dir = 0x0F;
    if (raw_state() & static_cast<byte_t>(JoypadButton::RIGHT))
      dir &= static_cast<byte_t>(~0x01);
    if (raw_state() & static_cast<byte_t>(JoypadButton::LEFT))
      dir &= static_cast<byte_t>(~0x02);
    if (raw_state() & static_cast<byte_t>(JoypadButton::UP))
      dir &= static_cast<byte_t>(~0x04);
    if (raw_state() & static_cast<byte_t>(JoypadButton::DOWN))
      dir &= static_cast<byte_t>(~0x08);
    low &= dir;
  }

  if ((select_bits & 0x20) == 0) {
    byte_t action = 0x0F;
    if (raw_state() & static_cast<byte_t>(JoypadButton::A))
      action &= static_cast<byte_t>(~0x01);
    if (raw_state() & static_cast<byte_t>(JoypadButton::B))
      action &= static_cast<byte_t>(~0x02);
    if (raw_state() & static_cast<byte_t>(JoypadButton::SELECT))
      action &= static_cast<byte_t>(~0x04);
    if (raw_state() & static_cast<byte_t>(JoypadButton::START))
      action &= static_cast<byte_t>(~0x08);
    low &= action;
  }

  return low;
}

void JOYP::update_output(const byte_t next_low) {
  if (if_reg) {
    if (const auto pressed = static_cast<byte_t>(last_low & ~next_low); pressed != 0)
      if_reg->put_flag(InterruptFlagMask::INT_FLAG_JOYPAD, true);
  }
  last_low = next_low;
}

} // namespace Joypad
