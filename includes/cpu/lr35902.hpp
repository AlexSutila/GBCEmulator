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
  LR35902(AddressBus *bus_ptr, std::optional<Debug::Debugger> &debugger,
          runtime_sys_info &sys);
  [[nodiscard]] const byte_t cur_opcode() const {
    return bus->read_byte(ins_base_addr, false);
  }
  [[nodiscard]] std::string disasm() const {
    if (ins_)
      return ins_->describe();

    // Edge case, there is nothing stopping frontends from calling this
    // before an instruction fetch, hence handle to avoid crash.
    return "";
  };
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

#endif // GBC_LR35902_HPP
