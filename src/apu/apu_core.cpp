#include "apu/apu.hpp"
#include "apu/apu_helpers.hpp"
#include "frontend/frontend.hpp"
#include "schedule.hpp"

#include <array>
#include <cstdint>

enum : std::uint16_t {
  F_AUDIO_REGISTERS = 1,
  F_AUDIO_UNUSED,
  F_WAVE_RAM,
  F_WAVE_RAM_BYTES,

  F_FRAME_SEQ_ACCUM_TCYCLES,
  F_FRAME_SEQ_STEP,
  F_CGB02_LENGTH_QUIRK,

  F_NR10,
  F_NR11,
  F_NR12,
  F_NR13,
  F_NR14,
  F_NR21,
  F_NR22,
  F_NR23,
  F_NR24,
  F_NR31,
  F_NR32,
  F_NR33,
  F_NR34,
  F_NR41,
  F_NR42,
  F_NR43,
  F_NR44,
  F_NR50,
  F_NR51,
  F_NR52,

  F_CH1,
  F_CH2,
  F_CH4,

  F_CH3_EN,
  F_CH3_TIMER,
  F_CH3_WAVE_BYTE_IDX,
  F_CH3_SAMPLE_BUFFER,

  F_CH1_LENGTH_COUNTER,
  F_CH2_LENGTH_COUNTER,
  F_CH3_LENGTH_COUNTER,
  F_CH4_LENGTH_COUNTER,

  F_CH1_ENV,
  F_CH2_ENV,
  F_CH4_ENV,

  F_CH1_SWEEP_SHADOW_FREQ,
  F_CH1_SWEEP_PERIOD,
  F_CH1_SWEEP_TIMER,
  F_CH1_SWEEP_SHIFT,
  F_CH1_SWEEP_NEGATE,
  F_CH1_SWEEP_ENABLED,
  F_CH1_SWEEP_NEGATE_USED,

  F_CH4_LFSR,
  F_MASTER_LEFT_CUR,
  F_MASTER_LEFT_TARGET,
  F_MASTER_LEFT_STEP,
  F_MASTER_RIGHT_CUR,
  F_MASTER_RIGHT_TARGET,
  F_MASTER_RIGHT_STEP,

  F_ROUTE_L_CUR,
  F_ROUTE_L_TARGET,
  F_ROUTE_L_STEP,
  F_ROUTE_R_CUR,
  F_ROUTE_R_TARGET,
  F_ROUTE_R_STEP,

  F_DC_X1_L,
  F_DC_Y1_L,
  F_DC_X1_R,
  F_DC_Y1_R,

  // Event scheduling
  F_SCHED,
};

template <typename T> void APU::parse_savestate(T &t) {
  constexpr auto version = 2; // Schema revision
  t.chunk_header(version, Savestate::C_APU);

  // Careful not to cause duplicate tags here, or it will break the savestate tree
  t.field_complex(F_AUDIO_REGISTERS, [&](T &t) {
    for (std::size_t i = 0; auto reg : audio_registers)
      t.field_complex(i++, [&](T &t) { reg.parse_savestate(t); });
  });
  t.field_complex(F_AUDIO_UNUSED, [&](T &t) {
    for (std::size_t i = 0; auto reg : audio_unused)
      t.field_complex(i++, [&](T &t) { reg.parse_savestate(t); });
  });
  t.field_complex(F_WAVE_RAM, [&](T &t) {
    for (std::size_t i = 0; auto reg : wave_ram)
      t.field_complex(i++, [&](T &t) { reg.parse_savestate(t); });
  });
  t.field_bytes(F_WAVE_RAM_BYTES, {wave_ram_bytes.data(), wave_ram_bytes.size()});

  t.field_generic(F_FRAME_SEQ_ACCUM_TCYCLES, frame_seq_accum_tcycles);
  t.field_generic(F_FRAME_SEQ_STEP, frame_seq_step);
  t.field_generic(F_CGB02_LENGTH_QUIRK, cgb02_length_quirk_);

  t.field_generic(F_NR10, nr10);
  t.field_generic(F_NR11, nr11);
  t.field_generic(F_NR12, nr12);
  t.field_generic(F_NR13, nr13);
  t.field_generic(F_NR14, nr14);
  t.field_generic(F_NR21, nr21);
  t.field_generic(F_NR22, nr22);
  t.field_generic(F_NR23, nr23);
  t.field_generic(F_NR24, nr24);
  t.field_generic(F_NR31, nr31);
  t.field_generic(F_NR32, nr32);
  t.field_generic(F_NR33, nr33);
  t.field_generic(F_NR34, nr34);
  t.field_generic(F_NR41, nr41);
  t.field_generic(F_NR42, nr42);
  t.field_generic(F_NR43, nr43);
  t.field_generic(F_NR44, nr44);
  t.field_generic(F_NR50, nr50);
  t.field_generic(F_NR51, nr51);
  t.field_generic(F_NR52, nr52);

  auto parse_ch = [&](T &t, ChannelState &c) {
    t.field_generic(1, c.enabled);
    t.field_generic(2, c.phase);
  };

  auto parse_env = [&](T &t, Envelope &e) {
    t.field_generic(1, e.volume);
    t.field_generic(2, e.period);
    t.field_generic(3, e.timer);
    t.field_generic(4, e.increase);
    t.field_generic(5, e.enabled);
  };

  auto parse_route = [&](T &t, std::array<float, 4> &arr) {
    // This is absolutely awful but hell with it lmao
    t.field_generic(1, arr.at(0));
    t.field_generic(2, arr.at(1));
    t.field_generic(3, arr.at(2));
    t.field_generic(4, arr.at(3));
  };

  t.field_complex(F_CH1, [&](T &t) { parse_ch(t, channel1); });
  t.field_complex(F_CH2, [&](T &t) { parse_ch(t, channel2); });
  t.field_complex(F_CH4, [&](T &t) { parse_ch(t, channel4); });

  t.field_generic(F_CH3_EN, channel3_enabled);
  t.field_generic(F_CH3_TIMER, ch3_timer);
  t.field_generic(F_CH3_WAVE_BYTE_IDX, ch3_wave_byte_index);
  t.field_generic(F_CH3_SAMPLE_BUFFER, ch3_sample_buffer);

  t.field_complex(F_CH1_ENV, [&](T &t) { parse_env(t, ch1_env); });
  t.field_complex(F_CH2_ENV, [&](T &t) { parse_env(t, ch2_env); });
  t.field_complex(F_CH4_ENV, [&](T &t) { parse_env(t, ch4_env); });

  t.field_generic(F_CH1_SWEEP_SHADOW_FREQ, ch1_sweep_shadow_freq);
  t.field_generic(F_CH1_SWEEP_PERIOD, ch1_sweep_period);
  t.field_generic(F_CH1_SWEEP_TIMER, ch1_sweep_timer);
  t.field_generic(F_CH1_SWEEP_SHIFT, ch1_sweep_shift);
  t.field_generic(F_CH1_SWEEP_NEGATE, ch1_sweep_negate);
  t.field_generic(F_CH1_SWEEP_ENABLED, ch1_sweep_enabled);
  t.field_generic(F_CH1_SWEEP_NEGATE_USED, ch1_sweep_negate_used);

  t.field_generic(F_CH4_LFSR, ch4_lfsr);
  t.field_generic(F_MASTER_LEFT_CUR, master_left_cur_);
  t.field_generic(F_MASTER_LEFT_STEP, master_left_step_);
  t.field_generic(F_MASTER_LEFT_TARGET, master_left_target_);
  t.field_generic(F_MASTER_RIGHT_CUR, master_right_cur_);
  t.field_generic(F_MASTER_RIGHT_STEP, master_right_step_);
  t.field_generic(F_MASTER_RIGHT_TARGET, master_right_target_);

  t.field_complex(F_ROUTE_L_CUR, [&](T &t) { parse_route(t, route_l_cur_); });
  t.field_complex(F_ROUTE_L_TARGET, [&](T &t) { parse_route(t, route_l_target_); });
  t.field_complex(F_ROUTE_L_STEP, [&](T &t) { parse_route(t, route_l_step_); });
  t.field_complex(F_ROUTE_R_CUR, [&](T &t) { parse_route(t, route_r_cur_); });
  t.field_complex(F_ROUTE_R_TARGET, [&](T &t) { parse_route(t, route_r_target_); });
  t.field_complex(F_ROUTE_R_STEP, [&](T &t) { parse_route(t, route_r_step_); });

  t.field_generic(F_DC_X1_L, dc_x1_l);
  t.field_generic(F_DC_Y1_L, dc_y1_l);
  t.field_generic(F_DC_X1_R, dc_x1_r);
  t.field_generic(F_DC_Y1_R, dc_y1_r);

  t.field_complex(F_SCHED, [&](T &t) {
    sched.parse_savestate(t, static_cast<std::size_t>(SchedulerEvent::EVENT_COUNT));
  });
  t.eof();
}

template void APU::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void APU::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void APU::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void APU::parse_savestate<Savestate::Checker>(Savestate::Checker &);

APU::APU(AddressBus &bus, Frontend &frontend, SystemScheduler &g_sched)
    : bus_(bus), frontend_(frontend), sched(g_sched, SchedulerComponent::SCHED_COMPONENT_APU,
                                            static_cast<std::size_t>(SchedulerEvent::EVENT_COUNT)) {
  mix_buffer.resize(frames_per_buffer * 2);
  register_mmio();

  // Initialize smoothed mixer state from power-on register values
  sync_mixer_targets_from_regs();

  // Kick off scheduler loop
  sched.schedule_event_on(0, APU::SchedulerEvent::EVENT_STEP_CYCLE);
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
    if (channel1.enabled) {
      if (const std::uint16_t f = ch1_frequency(); f < 2048) {
        const double hz = 131072.0 / (2048.0 - f);
        channel1.phase += hz / sample_rate_hz;
        if (channel1.phase >= 1.0)
          channel1.phase -= 1.0;
      }
    }

    // CH2 phase
    if (channel2.enabled) {
      if (const std::uint16_t f = ch2_frequency(); f < 2048) {
        const double hz = 131072.0 / (2048.0 - f);
        channel2.phase += hz / sample_rate_hz;
        if (channel2.phase >= 1.0)
          channel2.phase -= 1.0;
      }
    }

    // CH4 phase
    if (channel4.enabled) {
      if (const double hz = ch4_clock_hz(); hz > 0.0) {
        channel4.phase += hz / sample_rate_hz;
        while (channel4.phase >= 1.0) {
          channel4.phase -= 1.0;
          ch4_clock_lfsr();
        }
      }
    }
  }

  generate_sample();
}

ScheduledEventOutcome APU::handle_event(time_type event_time, unsigned event) {
  switch (static_cast<SchedulerEvent>(event)) {
  case SchedulerEvent::EVENT_STEP_CYCLE: {
    constexpr auto apu_tickrate_aligned = clks_static_timing(1);
    sched.schedule_event_on(event_time + apu_tickrate_aligned, SchedulerEvent::EVENT_STEP_CYCLE);

    step(); // TODO: This needs to be heavily optimized
  } break;

  default:
    __builtin_unreachable();
  }

  return ScheduledEventOutcome::EVENT_OUTCOME_NONE;
}
