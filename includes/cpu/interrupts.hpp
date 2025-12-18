#ifndef __INTERRUPTS_H
#define __INTERRUPTS_H

#include <memory/mmio.hpp>

enum class interruptFlagMask : byte_t {
  INT_FLAG_JOYPAD = 1u << 4,
  INT_FLAG_SERIAL = 1u << 3,
  INT_FLAG_TIMER = 1u << 2,
  INT_FLAG_LCD = 1u << 1,
  INT_FLAG_VBLANK = 1u << 0,
};

/*
 * Implements logic for both of the following MMIO registers:
 *  - 0xFF0F: Interrupt enable
 *  - 0xFFFF: Interrupt flag
 *
 * The interrupt enable register determines if the processor takes the jump to
 * the corresponding vector upon being triggered. The jump is not taken if the
 * master interrupt enable flag (see below) is disabled.
 *
 * The interrupt flag register is set when some component actually signals that
 * the interrupt should go off. A bit being set does nothing more than request
 * the execution of an interrupt, but ultimately whether or not that actually
 * happens depends on the enable flags.
 */
class InterruptBits : public MMIORegister {
public:
  void write(byte_t value) override;
  byte_t read() override;
  InterruptBits(const bool pull_unused_high);

  // Flag readers
  bool get_lcd() const { return lcd; }
  bool get_timer() const { return timer; }
  bool get_serial() const { return serial; }
  bool get_joypad() const { return joypad; }
  bool get_vblank() const { return vblank; }

  // Flag setters
  void put_vblank(bool enable) { vblank = enable; }
  void put_lcd(bool enable) { lcd = enable; }
  void put_timer(bool enable) { timer = enable; }
  void put_serial(bool enable) { serial = enable; }
  void put_joypad(bool enable) { joypad = enable; }

private:
  union {
    byte_t raw;
    struct {
      byte_t vblank : 1; // Bit 0
      byte_t lcd : 1;    // Bit 1
      byte_t timer : 1;  // Bit 2
      byte_t serial : 1; // Bit 3
      byte_t joypad : 1; // Bit 4
      byte_t unused : 3; // Bits 5–7
    };
  };
  const bool pull_high;
};

/*
 * IME: Interrupt master enable flag [Write Only]
 *
 * Flag internal to the CPU that controls whether *any* interrupt handlers are
 * called, regardless of the contents of the IE register. Only modifiable via:
 *  - Instruction `ei`: Enables flag
 *  - Instruction `di`: Disables flag
 *  - Instruction `reti`: Enables flag and returns
 *
 * IME is unset upon boot, and the effects of `ei` are delayed by one full
 * instruction. Note, that this is NOT a memory mapped IO-register, hence it
 * does not inherit `MMIORegister`.
 */
class InterruptMasterEnable {
public:
  InterruptMasterEnable();
  void enable(bool delayed);
  void disable();
  bool is_enabled() const;

  /* Call once per instruction */
  void step();

private:
  enum ImeStates {
    IME_PENDING,  /* IME is about to enter one instruction delay state */
    IME_DELAYED,  /* EI was invoked, delay for one instruction */
    IME_ENABLED,  /* IME is enabled */
    IME_DISABLED, /* IME is disabled */
  } ime_state;
};

#endif // __INTERRUPTS_H
