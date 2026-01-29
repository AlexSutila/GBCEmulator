#ifndef __LR35902_H
#define __LR35902_H

#include "cpu/instr/instr.hpp"
#include "cpu/interrupts.hpp"
#include "cpu/registers/regfile.hpp"
#include "debugger/breakpoint.hpp"
#include "debugger/debugger.hpp"
#include "memory/bus.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

struct runtime_sys_info;

/*
 * 8-bit 8080-like Sharp CPU (speculated to be a SM83 core), running
 * between 4.194304 MHz and 8.388608 MHz based on mode of operation
 */
class LR35902 final : private Debug::Debuggable {
public:
  LR35902(AddressBus *bus_ptr, std::optional<Debug::Debugger> &debugger,
          runtime_sys_info &sys);
  std::string disasm() const { return ins_->describe(); };
  void step();

  struct ProcessorState {
    addr_t pc;
    addr_t sp;
    byte_t a;
    byte_t b;
    byte_t c;
    byte_t d;
    byte_t e;
    byte_t f;
    byte_t h;
    byte_t l;
    bool ime_enabled;
  };
  void load_state(ProcessorState state);
  ProcessorState get_state() const;

private:
  static constexpr Debug::BreakReason brk_reason_flags =
      Debug::BRK_ADDRESS_EXECUTED | Debug::BRK_STEP_INSTRUCTION;
  RegisterFile reg_file{};
  AddressBus *const bus{};
  runtime_sys_info &sys_;

  /* Interrupt handling */
  std::optional<ISR *> should_interrupt();
  std::array<std::unique_ptr<ISR>, 5> isr_lookup{};
  template <InterruptFlagMask mask, InterruptVector vec>
  std::unique_ptr<ISR> mk_isr(); // Helper
  InterruptMasterEnable ime;
  InterruptBits ie_reg;
  InterruptBits if_reg;

  /* Opcode decoding configuration */
  using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
  void init_alu(lookup_table_t &lookup);
  void init_bitops(lookup_table_t &lookup);
  void init_branch(lookup_table_t &lookup);
  void init_control(lookup_table_t &lookup, runtime_sys_info &sys);
  void init_moves(lookup_table_t &lookup);
  lookup_table_t lookup{};

  enum CpuStates {
    STATE_FETCH,
    STATE_DECODE,
    STATE_EXECUTE,
    STATE_HALTED,
  } state;
  void do_fetch();
  void do_decode();
  void do_execute();
  void do_halt();

  addr_t ins_base_addr{}; // For debugger reference
  Instruction *ins_{};    // Reference to current ins

  std::optional<std::size_t> total_ins_clks{};
  std::size_t cur_ins_clks{};
};

#endif // __LR35902_H
