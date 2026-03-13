#include "apu/apu.hpp"
#include "apu/apu_helpers.hpp"

// ------------ Channel 1 Helpers ------------
void APU::trigger_channel1() {
  // If APU powered off, ignore triggers
  if ((nr52 & 0x80) == 0) {
    disable_channel1();
    return;
  }

  channel1.enabled = true;
  channel1.phase = 0.0;

  length_reload_if_zero<std::uint8_t>(ch1_length_counter, 64, (nr14 & 0x40) != 0,
                                      next_step_clocks_length());

  // Envelope
  trigger_envelope(nr12, ch1_env.volume, ch1_env.period, ch1_env.timer, ch1_env.increase,
                   ch1_env.enabled);

  // Sweep
  ch1_sweep_shadow_freq = ch1_frequency();
  ch1_sweep_period = static_cast<std::uint8_t>((nr10 >> 4) & 0x07);
  ch1_sweep_shift = static_cast<std::uint8_t>(nr10 & 0x07);
  ch1_sweep_negate = (nr10 & 0x08) != 0;
  ch1_sweep_timer = ch1_sweep_period == 0 ? 8 : ch1_sweep_period;
  ch1_sweep_enabled = ch1_sweep_period != 0 || ch1_sweep_shift != 0;
  ch1_sweep_negate_used = false;

  // Overflow check on trigger
  (void)ch1_sweep_overflow_check();

  // Disabled DAC doesn't prevent length reload/etc, but it does force channel off
  if (!ch1_dac_enabled())
    disable_channel1();
}

void APU::disable_channel1() {
  channel1.enabled = false;
  ch1_sweep_enabled = false;
  ch1_env.enabled = false;
}

bool APU::ch1_dac_enabled() const { return dac_enabled_pulse_noise(nr12); }

void APU::ch1_set_frequency(std::uint16_t freq) {
  freq &= 0x7FF;
  nr13 = static_cast<byte_t>(freq & 0xFF);
  nr14 = static_cast<byte_t>((nr14 & 0xF8) | ((freq >> 8) & 0x07));
}

std::uint16_t APU::ch1_frequency() const {
  return static_cast<std::uint16_t>(((nr14 & 0x07) << 8) | nr13);
}

bool APU::ch1_sweep_overflow_check() {
  if (!ch1_sweep_enabled || ch1_sweep_shift == 0)
    return false;

  bool overflow = false;
  (void)ch1_sweep_calculate(overflow);
  if (overflow) {
    disable_channel1();
    return true;
  }
  return false;
}

std::uint16_t APU::ch1_sweep_calculate(bool &overflow) {
  overflow = false;

  const std::uint16_t shadow = ch1_sweep_shadow_freq;
  const auto delta = static_cast<std::uint16_t>(shadow >> ch1_sweep_shift);

  auto next = static_cast<std::int32_t>(shadow);
  if (ch1_sweep_negate) {
    next -= delta;
    ch1_sweep_negate_used = true;
  } else {
    next += delta;
  }

  if (next >= 2048) {
    overflow = true;
    return 2048;
  }
  if (next < 0)
    next = 0;

  return static_cast<std::uint16_t>(next);
}

void APU::clock_ch1_length() {
  clock_length(nr14, ch1_length_counter, [this] { disable_channel1(); });
}

void APU::clock_ch1_envelope() {
  clock_envelope(ch1_env.volume, ch1_env.period, ch1_env.timer, ch1_env.increase, ch1_env.enabled);
}

void APU::clock_ch1_sweep() {
  if (!ch1_sweep_enabled)
    return;

  if (ch1_sweep_timer > 0)
    --ch1_sweep_timer;
  if (ch1_sweep_timer != 0)
    return;

  ch1_sweep_timer = ch1_sweep_period == 0 ? 8 : ch1_sweep_period;

  // period==0: timer runs, but NO sweep calculation/update occurs
  if (ch1_sweep_period == 0)
    return;

  // shift==0: on sweep clocks it still performs the calc/overflow-disable when period>0,
  // but must NOT update channel frequency when shift==0
  if (ch1_sweep_shift == 0) {
    bool overflow = false;
    (void)ch1_sweep_calculate(overflow);
    if (overflow)
      disable_channel1();
    return;
  }

  bool overflow = false;
  const std::uint16_t new_freq = ch1_sweep_calculate(overflow);
  if (overflow) {
    disable_channel1();
    return;
  }

  // Apply frequency and update shadow
  ch1_sweep_shadow_freq = new_freq;
  ch1_set_frequency(new_freq);

  // Second overflow check (hardware does a second calc after applying)
  (void)ch1_sweep_overflow_check();
}

// ------------ Sample Generation ------------
float APU::channel1_sample() const {
  if ((nr52 & 0x80) == 0 || !channel1.enabled || !ch1_dac_enabled())
    return 0.0f;

  const byte_t volume = ch1_env.volume;
  if (volume == 0)
    return 0.0f;

  if (const std::uint16_t frequency = ch1_frequency(); frequency >= 2048)
    return 0.0f;

  const auto duty_index = static_cast<std::uint8_t>((nr11 >> 6) & 0x03);
  const float duty = duty_table[duty_index];

  const float amp = static_cast<float>(volume) / 15.0f;
  return (channel1.phase < duty ? 1.0f : -1.0f) * amp;
}

// --------- Channel 2 Helpers -----------
bool APU::ch2_dac_enabled() const { return dac_enabled_pulse_noise(nr22); }

std::uint16_t APU::ch2_frequency() const {
  return static_cast<std::uint16_t>(((nr24 & 0x07) << 8) | nr23);
}

void APU::ch2_set_frequency(std::uint16_t freq) {
  freq &= 0x7FF;
  nr23 = static_cast<byte_t>(freq & 0xFF);
  nr24 = static_cast<byte_t>((nr24 & 0xF8) | ((freq >> 8) & 0x07));
}

void APU::disable_channel2() {
  channel2.enabled = false;
  ch2_env.enabled = false;
}

void APU::trigger_channel2() {
  if ((nr52 & 0x80) == 0) {
    disable_channel2();
    return;
  }

  channel2.enabled = true;
  channel2.phase = 0.0;

  length_reload_if_zero<std::uint8_t>(ch2_length_counter, 64, (nr24 & 0x40) != 0,
                                      next_step_clocks_length());

  trigger_envelope(nr22, ch2_env.volume, ch2_env.period, ch2_env.timer, ch2_env.increase,
                   ch2_env.enabled);

  if (!ch2_dac_enabled())
    disable_channel2();
}

void APU::clock_ch2_length() {
  clock_length(nr24, ch2_length_counter, [this] { disable_channel2(); });
}

void APU::clock_ch2_envelope() {
  clock_envelope(ch2_env.volume, ch2_env.period, ch2_env.timer, ch2_env.increase, ch2_env.enabled);
}

// ------------ Sample Generation ------------
float APU::channel2_sample() const {
  if ((nr52 & 0x80) == 0 || !channel2.enabled || !ch2_dac_enabled())
    return 0.0f;

  if (const std::uint16_t frequency = ch2_frequency(); frequency >= 2048)
    return 0.0f;

  const auto duty_index = static_cast<std::uint8_t>((nr21 >> 6) & 0x03);
  const float duty = duty_table[duty_index];

  const float amp = static_cast<float>(ch2_env.volume) / 15.0f;
  if (amp <= 0.0f)
    return 0.0f;

  return (channel2.phase < duty ? 1.0f : -1.0f) * amp;
}

// ----------- Channel 3 Helpers -----------
void APU::clock_ch3_length() {
  clock_length(nr34, ch3_length_counter, [this] { disable_channel3(); });
}

bool APU::ch3_dac_enabled() const { return (nr30 & 0x80) != 0; }

std::uint16_t APU::ch3_frequency() const {
  return static_cast<std::uint16_t>(((nr34 & 0x07) << 8) | nr33);
}

void APU::disable_channel3() { channel3_enabled = false; }

void APU::trigger_channel3() {
  if ((nr52 & 0x80) == 0) {
    disable_channel3();
    return;
  }

  channel3_enabled = true;
  ch3_wave_pos = 0;
  ch3_wave_byte_index = 0;

  // On trigger, CH3 outputs sample index 0 immediately, but the first timer
  // tick is delayed by an additional 3 APU cycles. Here: +6 T-cycles
  ch3_sample_buffer = wave_ram_bytes[0];

  const auto f = ch3_frequency();
  const auto period_tcycles = static_cast<std::uint16_t>((2048u - (f & 0x7FFu)) * 2u);
  ch3_timer = static_cast<std::uint16_t>(period_tcycles + 6u);

  length_reload_if_zero<std::uint16_t>(ch3_length_counter, 256, (nr34 & 0x40) != 0,
                                       next_step_clocks_length());

  if (!ch3_dac_enabled())
    disable_channel3();
}

// ------------ Sample Generation ------------
float APU::channel3_sample() const {
  if ((nr52 & 0x80) == 0 || !channel3_enabled || !ch3_dac_enabled())
    return 0.0f;

  // Output level code
  const std::uint8_t level = (nr32 >> 5) & 0x03;
  if (level == 0)
    return 0.0f;

  // Each wave RAM byte contains two 4-bit samples: high nibble first, then low
  const std::uint8_t raw4 = (ch3_wave_pos & 1u)
                                ? static_cast<std::uint8_t>(ch3_sample_buffer & 0x0Fu)
                                : static_cast<std::uint8_t>((ch3_sample_buffer >> 4) & 0x0Fu);

  // Center the 4-bit DAC output around 0, then apply the output level scaling
  float s = (static_cast<float>(raw4) - 7.5f) / 7.5f; // ~[-1, +1]
  if (level == 2)
    s *= 0.5f;
  else if (level == 3)
    s *= 0.25f;

  return s;
}

// ----------- Channel 4 Helpers -----------
void APU::clock_ch4_length() {
  clock_length(nr44, ch4_length_counter, [this] { disable_channel4(); });
}

void APU::clock_ch4_envelope() {
  clock_envelope(ch4_env.volume, ch4_env.period, ch4_env.timer, ch4_env.increase, ch4_env.enabled);
}

bool APU::ch4_dac_enabled() const { return dac_enabled_pulse_noise(nr42); }

void APU::disable_channel4() {
  channel4.enabled = false;
  ch4_env.enabled = false;
}

void APU::trigger_channel4() {
  if ((nr52 & 0x80) == 0) {
    disable_channel4();
    return;
  }

  channel4.enabled = true;
  channel4.phase = 0.0;
  ch4_lfsr = 0x7FFF; // reset all 1s

  length_reload_if_zero<std::uint8_t>(ch4_length_counter, 64, (nr44 & 0x40) != 0,
                                      next_step_clocks_length());

  trigger_envelope(nr42, ch4_env.volume, ch4_env.period, ch4_env.timer, ch4_env.increase,
                   ch4_env.enabled);

  if (!ch4_dac_enabled())
    disable_channel4();
}

double APU::ch4_clock_hz() const {
  double r = nr43 & 0x07;
  const int s = (nr43 >> 4) & 0x0F;

  if (r == 0)
    r = 0.5;

  // 262144 / r / 2^s
  return 262144.0 / r / static_cast<double>(1u << s);
}

void APU::ch4_clock_lfsr() {
  const std::uint16_t xor_bit = (ch4_lfsr ^ (ch4_lfsr >> 1)) & 0x0001;
  ch4_lfsr = (ch4_lfsr >> 1) | (xor_bit << 14);

  // width mode: also copy into bit 6 (7-bit LFSR)
  if (nr43 & 0x08)
    ch4_lfsr = static_cast<std::uint16_t>((ch4_lfsr & ~(1u << 6)) | (xor_bit << 6));
}

// ------------ Sample Generation ------------
float APU::channel4_sample() const {
  if ((nr52 & 0x80) == 0 || !channel4.enabled || !ch4_dac_enabled())
    return 0.0f;

  const float amp = static_cast<float>(ch4_env.volume) / 15.0f;
  if (amp <= 0.0f)
    return 0.0f;

  // Output is (inverted) bit0 in many emulator conventions
  const bool bit0 = (ch4_lfsr & 0x01) != 0;
  const float s = bit0 ? -1.0f : 1.0f;
  return s * amp;
}
