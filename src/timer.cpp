#include "timer.hpp"
#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include "savestate/codec.hpp"

enum : std::uint16_t {
  F_SYS_COUNTER = 1,
  F_TIMA,
  F_TMA,
  F_TAC,
  F_OVERFLOW_PENDING,
  F_OVERFLOW_DELAY,
  F_RELOAD_LATCH,
  F_RELOAD_DELAY,
};

template <typename T> void TimerUnit::parse_savestate(T &t) {
  constexpr auto version = 1; // Schema revision
  t.chunk_header(version, Savestate::C_TIMER);

  t.field_generic(F_SYS_COUNTER, sys_counter_);
  t.field_generic(F_TIMA, tima_);
  t.field_generic(F_TMA, tma_);
  t.field_generic(F_TAC, tac_);
  t.field_generic(F_OVERFLOW_PENDING, overflow_pending_);
  t.field_generic(F_OVERFLOW_DELAY, overflow_delay_);
  t.field_generic(F_RELOAD_LATCH, reload_latch_);
  t.field_generic(F_RELOAD_DELAY, reload_delay_);

  t.eof();
}

template void TimerUnit::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void TimerUnit::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void TimerUnit::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void TimerUnit::parse_savestate<Savestate::Checker>(Savestate::Checker &);

template <typename T> T *init_mmio(AddressBus *bus, const IORegisterMapping reg_id) {
  auto *reg = bus->get_mmio(reg_id);
  if (auto *casted = dynamic_cast<T *>(reg))
    return casted;
  throw std::logic_error(std::string("Failed to configure MMIO (Timer)"));
}

TimerUnit::TimerUnit(AddressBus *const bus)
    : tima_reg(*this), // Timer counter register
      tma_reg(*this),  // Timer modulo register
      tac_reg(*this),  // Timer control register
      div_reg(*this)   // Divider register
{
  using mmio = IORegisterMapping;
  using namespace PPU;

  /* Configure convenience MMIO register references */
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_TIMER_TIMA), &tima_reg);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_TIMER_TMA), &tma_reg);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_TIMER_TAC), &tac_reg);
  bus->connect_mmio(static_cast<addr_t>(mmio::MMIO_TIMER_DIV), &div_reg);

  /* Not owned by the pixel processing unit, so have to fetch references */
  if_reg = init_mmio<InterruptBits>(bus, mmio::MMIO_INT_FLAGS);
  reset();
}

void TimerUnit::reset() noexcept {
  sys_counter_ = 0;

  overflow_pending_ = false;
  overflow_delay_ = 0;

  reload_latch_ = false;
  reload_delay_ = 0;
}

byte_t TimerUnit::read_div() const noexcept {
  // DIV increments at 16384 Hz; in double-speed it's 32768 Hz
  // If sys_ increments once per "timer t-cycle", DIV is sys_[15:8].
  return static_cast<byte_t>((sys_counter_ >> 8) & 0xFF);
}

void TimerUnit::write_div() noexcept {
  // Writing any value resets DIV (and thus sys counter) and can cause an edge
  // tick
  const bool prev_in = edge_input(sys_counter_, tac_);
  sys_counter_ = 0;

  // falling edge -> tick
  if (const bool next_in = edge_input(sys_counter_, tac_); prev_in && !next_in) {
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

byte_t TimerUnit::read_tac() const noexcept { return static_cast<byte_t>(0xF8 | (tac_ & 0x07)); }

void TimerUnit::write_tac(byte_t v) noexcept {
  v &= 0x07;

  // "writing to TAC may increase TIMA once"
  const bool prev_in = edge_input(sys_counter_, tac_);
  tac_ = v;
  if (const bool next_in = edge_input(sys_counter_, tac_); prev_in && !next_in) {
    timer_tick_pulse();
  }
}

byte_t TimerUnit::tac_sel(const byte_t tac) noexcept { return static_cast<byte_t>(tac & 0x03); }

bool TimerUnit::tac_en(const byte_t tac) noexcept { return (tac & 0x04) != 0; }

bool TimerUnit::selected_bit(const std::uint16_t sys, const byte_t sel) noexcept {
  // This assumes sys_ increments at the "timer's base clock".
  // Mapping chosen so TAC rates line up with classic implementations:
  // 00: bit 9, 01: bit 3, 10: bit 5, 11: bit 7
  const int bit = (sel == 0) ? 9 : (sel == 1) ? 3 : (sel == 2) ? 5 : 7;
  return ((sys >> bit) & 1) != 0;
}

bool TimerUnit::edge_input(const std::uint16_t sys, const byte_t tac) noexcept {
  // On all models, TIMA increments on the falling edge of:
  //   (TAC.enable AND selected DIV bit)
  return tac_en(tac) && selected_bit(sys, tac_sel(tac));
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

  const bool prev = edge_input(sys_counter_, tac_);
  ++sys_counter_;

  if (const bool next = edge_input(sys_counter_, tac_); prev && !next) {
    timer_tick_pulse();
  }
}
