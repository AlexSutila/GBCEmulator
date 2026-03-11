#include "apu/apu_helpers.hpp"
#include "frontend/frontend.hpp"

#include <array>

APU::APU(AddressBus &bus, Frontend &frontend) : bus_(bus), frontend_(frontend) {
  mix_buffer.resize(frames_per_buffer * 2);
  register_mmio();

  // Initialize smoothed mixer state from power-on register values
  sync_mixer_targets_from_regs();
}

void APU::sync_mixer_targets_from_regs() {
  set_master_targets_from_nr50();
  set_route_targets_from_nr51();
}

void APU::set_master_targets_from_nr50() {
  const float master_left = static_cast<float>((nr50 >> 4) & 0x07) / 7.0f;
  const float master_right = static_cast<float>(nr50 & 0x07) / 7.0f;

  set_instant(master_left_cur_, master_left_target_, master_left_step_, master_left);
  set_instant(master_right_cur_, master_right_target_, master_right_step_, master_right);
}

void APU::set_route_targets_from_nr51() {
  // Right: bit0=CH1, bit1=CH2, bit2=CH3, bit3=CH4
  // Left : bit4=CH1, bit5=CH2, bit6=CH3, bit7=CH4
  for (std::size_t i = 0; i < 4; ++i) {
    const float l = (nr51 & (0x10u << i)) ? 1.0f : 0.0f;
    const float r = (nr51 & (0x01u << i)) ? 1.0f : 0.0f;
    set_instant(route_l_cur_[i], route_l_target_[i], route_l_step_[i], l);
    set_instant(route_r_cur_[i], route_r_target_[i], route_r_step_[i], r);
  }
}

void APU::advance_mixer_smoothing() {
  advance_ramp(master_left_cur_, master_left_target_, master_left_step_);
  advance_ramp(master_right_cur_, master_right_target_, master_right_step_);
  for (std::size_t i = 0; i < 4; ++i) {
    advance_ramp(route_l_cur_[i], route_l_target_[i], route_l_step_[i]);
    advance_ramp(route_r_cur_[i], route_r_target_[i], route_r_step_[i]);
  }
}

bool APU::next_step_clocks_length() const {
  const auto next = static_cast<std::uint8_t>((frame_seq_step + 1) & 0x07);
  return (next & 0x01u) == 0;
}

void APU::generate_sample() {
  // Smooth mixer parameters (NR50/NR51)
  advance_mixer_smoothing();

  float ch1 = channel1_sample();
  float ch2 = channel2_sample();
  float ch3 = channel3_sample();
  float ch4 = channel4_sample();

  // When APU is powered off, the output is forced to 0
  if ((nr52 & 0x80) == 0)
    ch1 = ch2 = ch3 = ch4 = 0.0f;

  const std::array<float, 4> ch{ch1, ch2, ch3, ch4};

  float left_raw = 0.0f;
  float right_raw = 0.0f;
  for (std::size_t i = 0; i < 4; ++i) {
    left_raw += ch[i] * route_l_cur_[i];
    right_raw += ch[i] * route_r_cur_[i];
  }

  // Apply master volume (NR50)
  float left = left_raw * master_left_cur_ * master_gain;
  float right = right_raw * master_right_cur_ * master_gain;

  // Remove DC offset.
  left = dc_block(left, dc_x1_l, dc_y1_l);
  right = dc_block(right, dc_x1_r, dc_y1_r);

  // Clamp.
  left = clamp_sample(left);
  right = clamp_sample(right);

  mix_buffer[frame_cursor * 2] = left;
  mix_buffer[frame_cursor * 2 + 1] = right;
  ++frame_cursor;

  if (frame_cursor >= frames_per_buffer) {
    frontend_.queue_audio_samples(mix_buffer.data(), mix_buffer.size());
    frame_cursor = 0;
  }
}

void APU::step_frame_sequencer() {
  // 512 Hz clock phase keeps running even when APU is off
  frame_seq_accum_tcycles += 1;
  while (frame_seq_accum_tcycles >= frame_sequencer_period_tcycles) {
    frame_seq_accum_tcycles -= frame_sequencer_period_tcycles;

    // When off, ignore the clock (step stays reset), but keep phase
    if (!apu_on_())
      continue;

    frame_seq_step = static_cast<std::uint8_t>((frame_seq_step + 1) & 0x07);

    // Frame sequencer schedule:
    // 0,2,4,6: length
    // 2,6: sweep (ch1 only)
    // 7: envelope
    switch (frame_seq_step) {
    case 0:
    case 2:
    case 4:
    case 6:
      clock_ch1_length();
      clock_ch2_length();
      clock_ch3_length();
      clock_ch4_length();
      break;
    default:
      break;
    }

    if (frame_seq_step == 2 || frame_seq_step == 6)
      clock_ch1_sweep();

    if (frame_seq_step == 7) {
      clock_ch1_envelope();
      clock_ch2_envelope();
      clock_ch4_envelope();
    }
  }
}

void APU::step() {
  step_frame_sequencer();

  if ((nr52 & 0x80) != 0 && channel3_enabled && ch3_dac_enabled()) {
    if (ch3_timer > 0)
      --ch3_timer;
    if (ch3_timer == 0) {
      // CH3 advances its 0..31 sample index every timer tick and refreshes the
      // 8-bit sample buffer from the currently selected wave RAM byte. This is
      // also the byte that CPU reads/writes are redirected to on CGB
      const auto f = ch3_frequency();
      const auto period_tcycles = static_cast<std::uint16_t>((2048u - (f & 0x7FFu)) * 2u);
      ch3_timer = period_tcycles;

      ch3_wave_pos = static_cast<std::uint8_t>((ch3_wave_pos + 1u) & 31u);
      ch3_wave_byte_index = static_cast<std::uint8_t>((ch3_wave_pos >> 1) & 0x0Fu);
      ch3_sample_buffer = wave_ram_bytes[ch3_wave_byte_index];
    }
  }

  constexpr double cycles_per_sample = cpu_clock_hz / sample_rate_hz;
  cycle_accumulator += 1.0;
  if (cycle_accumulator < cycles_per_sample)
    return;

  cycle_accumulator -= cycles_per_sample;

  if ((nr52 & 0x80) != 0) {
    // CH1 phase
    if (channel1_enabled) {
      if (const std::uint16_t f = ch1_frequency(); f < 2048) {
        const double hz = 131072.0 / (2048.0 - f);
        channel1_phase += hz / sample_rate_hz;
        if (channel1_phase >= 1.0)
          channel1_phase -= 1.0;
      }
    }

    // CH2 phase
    if (channel2_enabled) {
      if (const std::uint16_t f = ch2_frequency(); f < 2048) {
        const double hz = 131072.0 / (2048.0 - f);
        channel2_phase += hz / sample_rate_hz;
        if (channel2_phase >= 1.0)
          channel2_phase -= 1.0;
      }
    }

    // CH4 phase
    if (channel4_enabled) {
      if (const double hz = ch4_clock_hz(); hz > 0.0) {
        ch4_phase += hz / sample_rate_hz;
        while (ch4_phase >= 1.0) {
          ch4_phase -= 1.0;
          ch4_clock_lfsr();
        }
      }
    }
  }

  generate_sample();
}
