#ifndef GBC_INTERRUPTS_HPP
#define GBC_INTERRUPTS_HPP

#include "cpu/registers/regfile.hpp"
#include "emu_types.hpp"
#include "instr/instr.hpp"
#include "memory/mmio/mmio.hpp"
#include <tuple>

enum class InterruptFlagMask : byte_t {
  INT_FLAG_JOYPAD = 1u << 4,
  INT_FLAG_SERIAL = 1u << 3,
  INT_FLAG_TIMER = 1u << 2,
  INT_FLAG_LCD = 1u << 1,
  INT_FLAG_VBLANK = 1u << 0,
};

enum class InterruptVector : addr_t {
  INT_VECTOR_JOYPAD = 0x0060,
  INT_VECTOR_SERIAL = 0x0058,
  INT_VECTOR_TIMER = 0x0050,
  INT_VECTOR_LCD = 0x0048,
  INT_VECTOR_VBLANK = 0x0040,
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
 * the execution of an interrupt, but ultimately whether that actually
 * happens depends on the enable flags.
 */
class InterruptBits final : public MMIORegister {
public:
  void write(byte_t value) override;
  [[nodiscard]] byte_t peek() const override;
  byte_t read() override;
  explicit InterruptBits(bool pull_unused_high);

  void put_flag(InterruptFlagMask flag, bool value);
  [[nodiscard]] bool get_flag(InterruptFlagMask flag) const;

private:
  // This is set in the constructor, and does not change so we should not need
  // to serialize it. Just leave it alone and rely on good ol' inheritance.
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
  [[nodiscard]] bool is_enabled() const;
  [[nodiscard]] byte_t raw_state() const;
  void load_raw_state(byte_t state);

  /* Call once per instruction */
  void step();

private:
  enum ImeStates {
    IME_DELAYED,  /* EI was invoked, delay for one instruction */
    IME_ENABLED,  /* IME is enabled */
    IME_DISABLED, /* IME is disabled */
  } ime_state;
};

/*
 * Finally, this class implements the actual operation that handles interrupts.
 * By using the inheriting from `Instruction`, we are able to tie the handling
 * of interrupts with accurate timing into the LR35902's fetch/decode/execute
 * FSM seamlessly.
 *
 * The following interrupt service routine is executed when control is being
 * transferred to an interrupt handler:
 *
 * 1. Two wait steps are executed (8 clock cycles) pass, nothing happens
 * 2. The current value of the PC register is pushed onto the stack
 * 3. The PC register is set to the address of the handler
 *
 * The whole process consumes a fixed 20 clock cycles total, unless coming out
 * of the halted state in which case an additional 4 clock cycle delay is also
 * incurred.
 */
class ISR final : public Instruction {
public:
  ISR(RegisterFile *reg_file_ptr, AddressBus *bus_ptr, InterruptMasterEnable &ime,
      InterruptBits &if_reg, InterruptBits &ie_reg)
      : Instruction(reg_file_ptr, bus_ptr), ime_(ime), // Needed to disable interrupt master enable
        if_(if_reg),                                   // Determines which vector to jump to
        ie_(ie_reg) {}
  std::string describe() override;
  std::size_t exec() override;
  void incur_halt_delay(); // Invoked by CPU to incur when halted

  // Compute new PC location, considers stack overflow leading to EI overwrite
  // and the bizarre behavior that can emerge with that as well.
  using isr_metadata = std::tuple<InterruptFlagMask, InterruptVector>;
  [[nodiscard]] isr_metadata calc_effective_call_addr() const;
  void handle_ei_push_bug() const; // Occurs when interrupted with (SP == 0)

private:
  InterruptMasterEnable &ime_;
  InterruptBits &if_, &ie_;

  // Interrupt source is described by these fields
  bool halt_delay{false};
};

#endif // GBC_INTERRUPTS_HPP
