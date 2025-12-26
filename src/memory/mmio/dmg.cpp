#include "memory/mmio/dmg.hpp"
#include "emu_types.hpp"
#include <cassert>

namespace PPU {

void STAT::write(byte_t value) {
  // Most significant bit is un-mapped, preserve mode bits
  state = (state & 0x03) | (value & 0x7C) | 0x80;
}

byte_t STAT::read() {
  // Most significant bit is un-mapped
  return state | 0x80;
}

const StatModes STAT::get_mode() const {
  constexpr byte_t mode_mask = 0x03;
  const StatModes mode = static_cast<StatModes>(state & mode_mask);
  return mode;
}

void STAT::set_mode(StatModes mode) {
  constexpr byte_t mode_mask = 0x03;
  const byte_t mode_bits = static_cast<byte_t>(mode);
  state = state & ~mode_mask;
  state |= mode_bits;
}

void LY::write(byte_t) { /* Read only */ }

byte_t LY::read() {
  assert(state >= 0 && state <= max_ly());
  return state;
}

bool LY::inc() {
  if (state == max_ly()) {
    state = 0;
    return true;
  }
  ++state;
  return false;
}

}; // namespace PPU

/* Always start with boot ROM mapped */
BootROMCtrl::BootROMCtrl() : MMIORegister() { map_boot_rom = true; }

/* Writing this register disables the boot ROM */
void BootROMCtrl::write(const byte_t value) {
  MMIORegister::write(value);
  map_boot_rom = false;
}

byte_t BootROMCtrl::read() { return MMIORegister::read(); }

bool BootROMCtrl::boot_rom_enabled() const { return map_boot_rom; }


namespace Timer {

// ----- TimerUnit ------------------------------------------------------------

TimerUnit::TimerUnit(const bool cgb_model) : cgb_model_(cgb_model) { reset(); }

void TimerUnit::set_cgb_model(bool cgb_model) noexcept {
  cgb_model_ = cgb_model;
  reset();
}

void TimerUnit::connect_if(MMIORegister& if_reg) noexcept { if_reg_ = &if_reg; }

void TimerUnit::reset() noexcept {
  sys_ = 0;
  tima_ = 0;
  tma_  = 0;
  tac_  = 0;

  overflow_pending_ = false;
  overflow_delay_   = 0;

  reload_latch_ = false;
  reload_delay_ = 0;
}

void TimerUnit::tick_tcycles(const std::uint32_t tcycles, const bool double_speed) noexcept {
  // Note: double_speed param is supposed to be false, and it is expected that the outer loop handles "double speed"
  // logic. For interest of convenience and completeness, we support it here too
  // Normally it would tick only 1 cycle per call
  const std::uint32_t total = double_speed ? tcycles * 2 : tcycles;
  for (std::uint32_t i = 0; i < total; ++i) {
    advance_one_tcycle();
  }
}

byte_t TimerUnit::read_div() const noexcept {
  // DIV increments at 16384 Hz; in double-speed it's 32768 Hz
  // If sys_ increments once per "timer t-cycle", DIV is sys_[15:8].
  return static_cast<byte_t>((sys_ >> 8) & 0xFF);
}

void TimerUnit::write_div() noexcept {
  // Writing any value resets DIV (and thus sys counter) and can cause an edge tick
  const bool prev_in = edge_input(sys_, tac_);
  sys_ = 0;
  const bool next_in = edge_input(sys_, tac_);

  // falling edge -> tick (with CGB gating difference)
  if (prev_in && !next_in) {
    if (tick_allowed_on_fall()) timer_tick_pulse();
  }
}

byte_t TimerUnit::read_tima() const noexcept { return tima_; }

void TimerUnit::write_tima(const byte_t v) noexcept {
  // Cycle A/B rules: write during A cancels reload/IRQ; write during B ignored
  // Reading comprehension 101??
  if (reload_latch_) return;
  tima_ = v;
  if (overflow_pending_) {
    overflow_pending_ = false;
    overflow_delay_   = 0;
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
  const bool prev_in = edge_input(sys_, tac_);
  tac_ = v;
  const bool next_in = edge_input(sys_, tac_);
  if (prev_in && !next_in) {
    if (tick_allowed_on_fall()) timer_tick_pulse();
  }
}

byte_t TimerUnit::tac_sel(const byte_t tac) noexcept { return static_cast<byte_t>(tac & 0x03); }
bool  TimerUnit::tac_en(const byte_t tac)  noexcept { return (tac & 0x04) != 0; }

bool TimerUnit::selected_bit(const std::uint16_t sys, const byte_t sel) noexcept {
  // This assumes sys_ increments at the "timer's base clock".
  // Mapping chosen so TAC rates line up with classic implementations:
  // 00: bit 9, 01: bit 3, 10: bit 5, 11: bit 7
  const int bit =
      (sel == 0) ? 9 :
      (sel == 1) ? 3 :
      (sel == 2) ? 5 : 7;
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
  if (!if_reg_) return;
  byte_t v = if_reg_->read();
  v |= 0x04; // IF bit 2 = Timer
  if_reg_->write(v);
}

void TimerUnit::start_overflow_pipeline() noexcept {
  // Overflow reload/IRQ delayed by 1 M-cycle = 4 t-cycles
  overflow_pending_ = true;
  overflow_delay_   = 4;
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
  if (!tac_en(tac_)) return;
  if (overflow_pending_ || reload_latch_) return;

  if (tima_ == 0xFF) {
    tima_ = 0x00;
    start_overflow_pipeline();
  } else {
    ++tima_;
  }
}

void TimerUnit::advance_one_tcycle() noexcept {
  service_overflow_pipeline();

  const bool prev = edge_input(sys_, tac_);
  ++sys_;
  const bool next = edge_input(sys_, tac_);

  if (prev && !next) {
    if (tick_allowed_on_fall()) timer_tick_pulse();
  }
}

// ----- MMIO facades (inherit MMIORegister) ----------------------------------

DIV::DIV(TimerUnit& t) : MMIORegister{}, t_(t) {}
void DIV::write(byte_t) { t_.write_div(); }
byte_t DIV::read() { return t_.read_div(); }

TIMA::TIMA(TimerUnit& t) : MMIORegister{}, t_(t) {}
void TIMA::write(byte_t v) { t_.write_tima(v); }
byte_t TIMA::read() { return t_.read_tima(); }

TMA::TMA(TimerUnit& t) : MMIORegister{}, t_(t) {}
void TMA::write(byte_t v) { t_.write_tma(v); }
byte_t TMA::read() { return t_.read_tma(); }

TAC::TAC(TimerUnit& t) : MMIORegister{}, t_(t) {}
void TAC::write(byte_t v) { t_.write_tac(v); }
byte_t TAC::read() { return t_.read_tac(); }

} // namespace Timer
