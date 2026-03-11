#ifndef GBC_APU_HELPER_HPP
#define GBC_APU_HELPER_HPP

#include "emu_types.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>

// Power-on register values (GB/CGB)
inline constexpr byte_t power_on_nr50 = 0x77;
inline constexpr byte_t power_on_nr51 = 0xF3;
inline constexpr byte_t power_on_nr52 = 0x80;

// Mixer
inline constexpr float master_gain = 0.25f;

// Pulse duty fractions
inline constexpr std::array duty_table{0.125f, 0.25f, 0.5f, 0.75f};

[[nodiscard]] inline float clamp_sample(const float v) { return std::clamp(v, -1.0f, 1.0f); }

// 1-pole DC blocker
[[nodiscard]] inline float dc_block(const float x, float &x1, float &y1) {
  constexpr float R = 0.995f;
  const float y = x - x1 + R * y1;
  x1 = x;
  y1 = y;
  return y;
}

inline void advance_ramp(float &cur, const float target, float &step) {
  if (step == 0.0f)
    return;

  const float next = cur + step;
  if ((step > 0.0f && next >= target) || (step < 0.0f && next <= target)) {
    cur = target;
    step = 0.0f;
    return;
  }
  cur = next;
}

inline void set_instant(float &cur, float &target, float &step, const float new_target) {
  target = new_target;
  cur = target;
  step = 0.0f;
}

[[nodiscard]] inline bool dac_enabled_pulse_noise(const byte_t nrx2) { return (nrx2 & 0xF8) != 0; }

inline void trigger_envelope(const byte_t nrx2, std::uint8_t &volume, std::uint8_t &period,
                             std::uint8_t &timer, bool &increase, bool &enabled) {
  volume = static_cast<std::uint8_t>((nrx2 >> 4) & 0x0F);
  increase = (nrx2 & 0x08) != 0;
  period = static_cast<std::uint8_t>(nrx2 & 0x07);
  timer = period == 0 ? 8 : period;
  enabled = period != 0;
}

inline void clock_envelope(std::uint8_t &volume, const std::uint8_t period, std::uint8_t &timer,
                           const bool increase, bool &enabled) {
  if (!enabled)
    return;

  if (timer > 0)
    --timer;
  if (timer != 0)
    return;

  timer = period == 0 ? 8 : period;

  if (increase) {
    if (volume < 15)
      ++volume;
    else
      enabled = false;
  } else {
    if (volume > 0)
      --volume;
    else
      enabled = false;
  }
}

template <class T, class DisableFn>
void clock_length(const byte_t nrx4, T &counter, DisableFn &&disable) {
  static_assert(std::is_integral_v<T>);
  if ((nrx4 & 0x40) == 0 || counter == 0)
    return;
  --counter;
  if (counter == 0)
    disable();
}

template <class T, class DisableFn>
void maybe_extra_length_clock(const bool next_step_clocks_length, const bool prev_len_en,
                              const bool new_len_en, const bool cgb02_length_quirk, T &counter,
                              const bool trigger, DisableFn &&disable) {
  static_assert(std::is_integral_v<T>);

  if (next_step_clocks_length)
    return;
  if (prev_len_en)
    return;
  if (!(new_len_en || cgb02_length_quirk))
    return;
  if (counter == 0)
    return;

  --counter;
  if (counter == 0 && !trigger)
    disable();
}

template <class T>
void length_reload_if_zero(T &counter, const T max_value, const bool length_enabled,
                           const bool next_step_clocks_length) {
  static_assert(std::is_integral_v<T>);
  if (counter != 0)
    return;
  counter = static_cast<T>(
      (length_enabled && !next_step_clocks_length) ? static_cast<T>(max_value - 1) : max_value);
}

#endif // GBC_APU_HELPER_HPP
