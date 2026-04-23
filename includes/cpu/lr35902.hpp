#ifndef GBC_LR35902_HPP
#define GBC_LR35902_HPP

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
class LR35902 final : Debug::Debuggable {
public:
  LR35902(AddressBus *bus_ptr, std::optional<Debug::Debugger> &debugger, runtime_sys_info &sys);
  [[nodiscard]] byte_t cur_opcode() const { return bus->read_byte(ins_base_addr, false); }
  [[nodiscard]] std::string disasm() const {
    if (ins_)
      return ins_->describe();

    // Edge case, there is nothing stopping frontends from calling this
    // before an instruction fetch, hence handle to avoid crash.
    return "";
  };

  /**
   * Callback is used to synchronize GBC peripherals. I tried, so hard, to
   * not template this. I will not use `std::function`, C++ is the bain of
   * my existence.
   */
  template <typename F> std::size_t big_step(F &&psync_cb) {
    switch (state) {
    case STATE_FETCH: // Break omitted intentionally to emulate fetch/exec overlap
      do_fetch();

    case STATE_EXECUTE: {
      const std::size_t total_cycles = timing_info.total_cycles;
      const std::size_t sync_events = timing_info.sync_events;
      std::size_t elapsed_cycles{0};

      // Handle each sync event
      for (std::size_t sync_event{0}; sync_event < sync_events; ++sync_event) {
        const std::size_t sync_cycle = ins_->next_sync_cycle();
        psync_cb(sync_cycle - elapsed_cycles);

        ins_->exec(); // Handle event, prime next event
        elapsed_cycles = sync_cycle;
      }

      // Account for remaining cycles up until next opcode fetch
      const std::size_t final_sync_cycles = total_cycles - elapsed_cycles;
      psync_cb(final_sync_cycles);

      // Finally, we still have to perform the state transition as per cycle-stepped impl
      do_exec_state_transition();
      return elapsed_cycles;
    };

    case STATE_HALTED:
      do_halt();

      psync_cb(1); // TODO: We can do a lot better here
      return 1;
    }

    // Safe since we return instead of breaking
    __builtin_unreachable();
  }

  /**
   * Basically just a more stable version of big_step(), executes a single
   * clock cycle at a time, regardless of whether its an idle cycle or not.
   */
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
  void load_state(ProcessorState state_);
  [[nodiscard]] ProcessorState get_state() const;

  template <typename T> void parse_savestate(T &t);
  [[nodiscard]] bool savestate_ready() const;

private:
  static constexpr Debug::BreakReason brk_reason_flags =
      Debug::BRK_ADDRESS_EXECUTED | Debug::BRK_STEP_INSTRUCTION;
  RegisterFile reg_file{};
  AddressBus *const bus{};
  runtime_sys_info &sys_;

  /* Interrupt handling */
  [[nodiscard]] bool should_interrupt() const;
  InterruptMasterEnable ime;
  InterruptBits ie_reg;
  InterruptBits if_reg;
  ISR isr;

  /* Opcode decoding configuration */
  using lookup_table_t = std::array<std::unique_ptr<Instruction>, 256>;
  void init_alu(lookup_table_t &lookup_);
  void init_bitops(lookup_table_t &lookup_);
  void init_branch(lookup_table_t &lookup_);
  void init_control(lookup_table_t &lookup_, runtime_sys_info &sys);
  void init_moves(lookup_table_t &lookup_);
  lookup_table_t lookup{};

  enum CpuStates {
    STATE_FETCH,
    STATE_EXECUTE,
    STATE_HALTED,
  } state;
  void do_exec_state_transition();
  void do_fetch();
  void do_halt();

  void prime_next_instr(Instruction *const next_ins);
  InstructionTiming timing_info{};
  std::size_t cur_ins_clks{};

  addr_t ins_base_addr{}; // For debugger reference
  Instruction *ins_{};    // Reference to current ins
};

#endif // GBC_LR35902_HPP
