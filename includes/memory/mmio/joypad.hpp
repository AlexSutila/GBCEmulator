#ifndef GBC_JOYPAD_HPP
#define GBC_JOYPAD_HPP

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"

class InterruptBits;

namespace Joypad {

enum class JoypadButton : byte_t {
  RIGHT = 1 << 0,
  LEFT = 1 << 1,
  UP = 1 << 2,
  DOWN = 1 << 3,
  A = 1 << 4,
  B = 1 << 5,
  SELECT = 1 << 6,
  START = 1 << 7,
};

class JOYP final : public MMIORegister {
public:
  JOYP();
  void write(byte_t value) override;
  byte_t read() override;

  void set_button(JoypadButton button, bool pressed);
  void set_state(byte_t mask);
  void set_interrupt_reg(InterruptBits *reg);

private:
  byte_t compute_low_bits() const;
  void update_output(byte_t next_low);

  byte_t select_bits{};
  byte_t button_state{};
  byte_t last_low{};
  InterruptBits *if_reg{};
};

} // namespace Joypad

#endif // GBC_JOYPAD_HPP
