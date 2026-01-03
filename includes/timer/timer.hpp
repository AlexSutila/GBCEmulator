#ifndef __TIMER_H
#define __TIMER_H

#include "cpu/interrupts.hpp"
#include "emu_types.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"

struct runtime_sys_info;

class TimerUnit {
public:
  explicit TimerUnit(AddressBus *const bus, runtime_sys_info &sys,
                     bool cgb_model = true);
  void reset() noexcept;
  void step() noexcept;

  // MMIO-facing helpers
  [[nodiscard]] byte_t read_div() const noexcept;
  void write_div() noexcept;

  [[nodiscard]] byte_t read_tima() const noexcept;
  void write_tima(byte_t v) noexcept;

  [[nodiscard]] byte_t read_tma() const noexcept;
  void write_tma(byte_t v) noexcept;

  [[nodiscard]] byte_t read_tac() const noexcept;
  void write_tac(byte_t v) noexcept;

private:
  [[nodiscard]] static byte_t tac_sel(byte_t tac) noexcept;
  [[nodiscard]] static bool tac_en(byte_t tac) noexcept;
  [[nodiscard]] static bool selected_bit(std::uint16_t sys,
                                         byte_t sel) noexcept;

  [[nodiscard]] bool edge_input(std::uint16_t sys, byte_t tac) const noexcept;
  [[nodiscard]] bool tick_allowed_on_fall() const noexcept;

  void start_overflow_pipeline() noexcept;
  void service_overflow_pipeline() noexcept;
  void request_timer_irq() const noexcept;
  void timer_tick_pulse() noexcept;

  InterruptBits *if_reg{};
  bool cgb_model_{true};

  Timer::TIMA tima_reg;
  Timer::TMA tma_reg;
  Timer::TAC tac_reg;
  Timer::DIV div_reg;

  std::uint16_t sys_counter_{};
  byte_t tima_{};
  byte_t tma_{};
  byte_t tac_{};

  // Overflow "cycle A/B"
  bool overflow_pending_{};
  std::uint8_t overflow_delay_{};

  bool reload_latch_{};
  std::uint8_t reload_delay_{};

  runtime_sys_info &sys_;
};

#endif // __TIMER_H
