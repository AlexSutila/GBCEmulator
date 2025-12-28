#include "timer/timer.hpp"
#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"

TimerUnit::TimerUnit(AddressBus *const bus_ptr, const bool cgb_model)
    : cgb_model_(cgb_model), // Since we emulate a GameBoyColor, always true
      tima_reg(*this),       // Timer counter register
      tma_reg(*this),        // Timer modulo register
      tac_reg(*this),        // Timer control register
      div_reg(*this)         // Divider register
{
  using mmio = IORegisterMapping;
  using namespace PPU;

  /* Configure convenience MMIO register references */
  bus_ptr->connect_mmio(static_cast<addr_t>(mmio::MMIO_TIMER_TIMA), &tima_reg);
  bus_ptr->connect_mmio(static_cast<addr_t>(mmio::MMIO_TIMER_TMA), &tma_reg);
  bus_ptr->connect_mmio(static_cast<addr_t>(mmio::MMIO_TIMER_TAC), &tac_reg);
  bus_ptr->connect_mmio(static_cast<addr_t>(mmio::MMIO_TIMER_DIV), &div_reg);

  /* Not owned by the pixel processing unit, so have to fetch references */
  if_reg = init_mmio<InterruptBits>(bus_ptr, mmio::MMIO_INT_FLAGS);
  reset();
}

void TimerUnit::reset() noexcept {
  sys_ = 0;

  overflow_pending_ = false;
  overflow_delay_ = 0;

  reload_latch_ = false;
  reload_delay_ = 0;
}

byte_t TimerUnit::read_div() const noexcept {
  // DIV increments at 16384 Hz; in double-speed it's 32768 Hz
  // If sys_ increments once per "timer t-cycle", DIV is sys_[15:8].
  return static_cast<byte_t>((sys_ >> 8) & 0xFF);
}

void TimerUnit::write_div() noexcept {
  // Writing any value resets DIV (and thus sys counter) and can cause an edge
  // tick
  const bool prev_in = edge_input(sys_, tac_);
  sys_ = 0;
  const bool next_in = edge_input(sys_, tac_);

  // falling edge -> tick (with CGB gating difference)
  if (prev_in && !next_in) {
    if (tick_allowed_on_fall())
      timer_tick_pulse();
  }
}

byte_t TimerUnit::read_tima() const noexcept { return tima_; }

void TimerUnit::write_tima(const byte_t v) noexcept {
  // Cycle A/B rules: write during A cancels reload/IRQ; write during B ignored
  // Reading comprehension 101??
  if (reload_latch_)
    return;
  tima_ = v;
  if (overflow_pending_) {
    overflow_pending_ = false;
    overflow_delay_ = 0;
  }
}

byte_t TimerUnit::read_tma() const noexcept { return tma_; }

void TimerUnit::write_tma(const byte_t v) noexcept {
  tma_ = v;
  if (reload_latch_) {
    tima_ = tma_; // during cycle B, TIMA tracks TMA at end of cycle
  }
}

byte_t TimerUnit::read_tac() const noexcept {
  return static_cast<byte_t>(0xF8 | (tac_ & 0x07));
}

void TimerUnit::write_tac(byte_t v) noexcept {
  v &= 0x07;

  // "writing to TAC may increase TIMA once"
  const bool prev_in = edge_input(sys_, tac_);
  tac_ = v;
  const bool next_in = edge_input(sys_, tac_);
  if (prev_in && !next_in) {
    if (tick_allowed_on_fall())
      timer_tick_pulse();
  }
}

byte_t TimerUnit::tac_sel(const byte_t tac) noexcept {
  return static_cast<byte_t>(tac & 0x03);
}
bool TimerUnit::tac_en(const byte_t tac) noexcept { return (tac & 0x04) != 0; }

bool TimerUnit::selected_bit(const std::uint16_t sys,
                             const byte_t sel) noexcept {
  // This assumes sys_ increments at the "timer's base clock".
  // Mapping chosen so TAC rates line up with classic implementations:
  // 00: bit 9, 01: bit 3, 10: bit 5, 11: bit 7
  const int bit = (sel == 0) ? 9 : (sel == 1) ? 3 : (sel == 2) ? 5 : 7;
  return ((sys >> bit) & 1) != 0;
}

bool TimerUnit::edge_input(std::uint16_t sys, byte_t tac) const noexcept {
  const bool src = selected_bit(sys, tac_sel(tac));
  if (cgb_model_) {
    // CGB: edge detector before enable gating (enable applied after)
    return src;
  }
  // DMG: edge detector sees (src AND enable)
  return src && tac_en(tac);
}

bool TimerUnit::tick_allowed_on_fall() const noexcept {
  // CGB: enable is after edge detector -> only tick if enabled now
  return !cgb_model_ || tac_en(tac_);
}

void TimerUnit::request_timer_irq() const noexcept {
  if (!if_reg)
    return;
  if_reg->put_flag(InterruptFlagMask::INT_FLAG_TIMER, true);
}

void TimerUnit::start_overflow_pipeline() noexcept {
  // Overflow reload/IRQ delayed by 1 M-cycle = 4 t-cycles
  overflow_pending_ = true;
  overflow_delay_ = 4;
}

void TimerUnit::service_overflow_pipeline() noexcept {
  if (overflow_pending_) {
    if (overflow_delay_ > 0 && --overflow_delay_ == 0) {
      overflow_pending_ = false;

      // cycle B window (TIMA writes ignored)
      reload_latch_ = true;
      reload_delay_ = 4;

      tima_ = tma_;
      request_timer_irq();
    }
  } else if (reload_latch_) {
    if (reload_delay_ > 0 && --reload_delay_ == 0) {
      reload_latch_ = false;
    }
  }
}

void TimerUnit::timer_tick_pulse() noexcept {
  if (!tac_en(tac_))
    return;
  if (overflow_pending_ || reload_latch_)
    return;

  if (tima_ == 0xFF) {
    tima_ = 0x00;
    start_overflow_pipeline();
  } else {
    ++tima_;
  }
}

void TimerUnit::step() noexcept {
  service_overflow_pipeline();

  const bool prev = edge_input(sys_, tac_);
  ++sys_;
  const bool next = edge_input(sys_, tac_);

  if (prev && !next) {
    if (tick_allowed_on_fall())
      timer_tick_pulse();
  }
}
