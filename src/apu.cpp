#include "apu.hpp"
#include "frontend/frontend.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/mmio.hpp"
#include <algorithm>

namespace {
  constexpr addr_t audio_base =
    static_cast<addr_t>(IORegisterMapping::MMIO_AUDIO_BASE);
  constexpr std::size_t audio_register_count = 0x17;
  constexpr std::size_t audio_unused_count = 0x09; // FF27-FF2F
  constexpr std::size_t wave_ram_size = 0x10;
  constexpr float master_gain = 0.25f;
  constexpr byte_t power_on_nr50 = 0x77;
  constexpr byte_t power_on_nr51 = 0xF3;
  constexpr byte_t power_on_nr52 = 0x80;

  constexpr std::array duty_table{0.125f, 0.25f, 0.5f, 0.75f};

  float clamp_sample(const float v) {
    return std::clamp(v, -1.0f, 1.0f);
  }
} // namespace

static float dc_block(const float x, float& x1, float& y1) {
  constexpr float R = 0.995f;
  const float y = x - x1 + R * y1;
  x1 = x;
  y1 = y;
  return y;
}

// ReSharper disable CppDFAUnreachableCode
void Audio::AudioRegister::configure(const byte_t initial, WriteCallback on_write_cb,
                                     ReadCallback on_read_cb) {
  state = initial;
  on_write = std::move(on_write_cb);
  on_read = std::move(on_read_cb);
}

static void advance_ramp(float& cur, const float target, float& step) {
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

void Audio::AudioRegister::write(const byte_t value) {
  state = value;
  if (on_write)
    on_write(value);
}

byte_t Audio::AudioRegister::read() {
  if (on_read)
    return on_read(state);
  return state;
}

APU::APU(AddressBus& bus, Frontend& frontend)
  : bus_(bus), frontend_(frontend) {
  mix_buffer.resize(frames_per_buffer * 2);
  register_mmio();

  // Initialize smoothed mixer state from power-on register values
  sync_mixer_targets_from_regs();
}

void APU::power_off_reset_regs_() {
  // When NR52 is turned off, hardware clears all APU regs (wave RAM unaffected).
  nr10 = nr11 = nr12 = nr13 = nr14 = 0;
  nr21 = nr22 = nr23 = nr24 = 0;
  nr30 = nr31 = nr32 = nr33 = nr34 = 0;
  nr41 = nr42 = nr43 = nr44 = 0;
  nr50 = nr51 = 0;

  disable_channel1();
  disable_channel2();
  disable_channel3();
  disable_channel4();

  channel1_phase = 0.0;
  channel2_phase = 0.0;
  ch4_phase = 0.0;
  ch4_lfsr = 0x7FFF;

  ch3_wave_pos = 0;
  ch3_timer = 0;
  ch3_wave_byte_index = 0;
  ch3_sample_buffer = 0;

  ch1_length_counter = 0;
  ch2_length_counter = 0;
  ch3_length_counter = 0;
  ch4_length_counter = 0;

  ch1_env_volume = ch1_env_period = ch1_env_timer = 0;
  ch1_env_increase = false;
  ch1_env_enabled = false;
  ch2_env_volume = ch2_env_period = ch2_env_timer = 0;
  ch2_env_increase = false;
  ch2_env_enabled = false;
  ch4_env_volume = ch4_env_period = ch4_env_timer = 0;
  ch4_env_increase = false;
  ch4_env_enabled = false;

  ch1_sweep_shadow_freq = 0;
  ch1_sweep_period = ch1_sweep_timer = ch1_sweep_shift = 0;
  ch1_sweep_negate = false;
  ch1_sweep_enabled = false;
  ch1_sweep_negate_used = false;

  // Keep mixer smoothing in sync with cleared regs
  sync_mixer_targets_from_regs();
}

void APU::sync_mixer_targets_from_regs() {
  set_master_targets_from_nr50();
  set_route_targets_from_nr51();
}

void APU::set_master_targets_from_nr50() {
  const float master_left = static_cast<float>(nr50 >> 4 & 0x07) / 7.0f;
  const float master_right = static_cast<float>(nr50 & 0x07) / 7.0f;

  auto set = [&](float& cur, float& target, float& step, const float new_target) {
    target = new_target;
    cur = target;
    step = 0.0f;
  };

  set(master_left_cur_, master_left_target_, master_left_step_, master_left);
  set(master_right_cur_, master_right_target_, master_right_step_, master_right);
}

void APU::set_route_targets_from_nr51() {
  auto set = [&](float& cur, float& target, float& step, const float new_target) {
    target = new_target;
    cur = target;
    step = 0.0f;
  };

  // Right: bit0=CH1, bit1=CH2, bit2=CH3, bit3=CH4
  // Left : bit4=CH1, bit5=CH2, bit6=CH3, bit7=CH4
  for (std::size_t i = 0; i < 4; ++i) {
    const float l = (nr51 & (0x10u << i)) ? 1.0f : 0.0f;
    const float r = (nr51 & (0x01u << i)) ? 1.0f : 0.0f;
    set(route_l_cur_[i], route_l_target_[i], route_l_step_[i], l);
    set(route_r_cur_[i], route_r_target_[i], route_r_step_[i], r);
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
  const auto next =
      static_cast<std::uint8_t>((frame_seq_step + 1) & 0x07);
  return (next & 0x01u) == 0;
}

void APU::register_mmio() {
  nr50 = power_on_nr50;
  nr51 = power_on_nr51;
  nr52 = power_on_nr52;

  for (std::size_t i = 0; i < audio_register_count; ++i)
    bus_.connect_mmio(static_cast<addr_t>(audio_base + i), &audio_registers[i]);
  for (std::size_t i = 0; i < audio_unused_count; ++i)
    bus_.connect_mmio(static_cast<addr_t>(audio_base + audio_register_count + i),
                      &audio_unused[i]);
  for (std::size_t i = 0; i < wave_ram_size; ++i) {
      bus_.connect_mmio(
        static_cast<addr_t>(audio_base + audio_register_count + audio_unused_count + i),
        &wave_ram[i]
      );
  }
  for (auto& b : wave_ram_bytes) b = 0;

  // Wave RAM is accessible even when NR52 is off
  // On CGB, while CH3 is playing, accesses are redirected to the byte selected
  // by the current waveform position
  for (std::size_t i = 0; i < wave_ram_size; ++i) {
    wave_ram[i].configure(
      0x00,
      [this, i](const byte_t v) {
        // CGB: while CH3 is active, all wave RAM accesses are redirected to
        // the *currently selected* waveform byte.
        const std::size_t dst =
          channel3_enabled? static_cast<std::size_t>(ch3_wave_pos >> 1 & 0x0Fu) : i;
        wave_ram_bytes[dst] = v;
      },
      [this, i](byte_t) -> byte_t {
        const std::size_t src =
          channel3_enabled? static_cast<std::size_t>(ch3_wave_pos >> 1 & 0x0Fu) : i;
        return wave_ram_bytes[src];
      }
    );
  }

  // Unused audio area FF27-FF2F reads back as $FF and ignores writes
  for (auto& r : audio_unused)
    r.configure(0xFF, [](byte_t) {
                }, [](byte_t) {
                  return static_cast<byte_t>(0xFF);
                });

  // --- Channel 1 (NR10-NR14 / FF10-FF14) ---
  audio_registers[0x00].configure(
    0x00,
    [this](const byte_t value) {
      if (!apu_on_())
        return;

      const byte_t old = nr10;
      nr10 = value;

      // Sweep negate quirk: if negation was used, clearing negate disables CH1
      if (channel1_enabled) {
        const bool old_neg = (old & 0x08) != 0;
        if (const bool new_neg = (value & 0x08) != 0;
          ch1_sweep_negate_used && old_neg && !new_neg) {
          disable_channel1();
          return;
        }
      }
      // Sweep enabled is latched on trigger; NR10 writes can't enable it later
      // If it WAS enabled, NR10 writes update sweep parameters
      if (ch1_sweep_enabled) {
        ch1_sweep_period = static_cast<std::uint8_t>((nr10 >> 4) & 0x07);
        ch1_sweep_shift  = static_cast<std::uint8_t>(nr10 & 0x07);
        ch1_sweep_negate = (nr10 & 0x08) != 0;
        // Don't reload ch1_sweep_timer here (hardware doesn't)
      }
    },
    [this](byte_t) { return static_cast<byte_t>(nr10 | 0x80); }
  );

  audio_registers[0x01].configure(
    0x00,
    [this](const byte_t value) {
      if (!apu_on_())
        return;
      nr11 = value;
      ch1_length_counter = static_cast<std::uint8_t>(64 - (value & 0x3F));
    },
    [this](byte_t) { return static_cast<byte_t>(nr11 | 0x3F); }
  );

  audio_registers[0x02].configure(
    0x00,
    [this](const byte_t value) {
      if (!apu_on_())
        return;
      nr12 = value;
      if (!ch1_dac_enabled())
        disable_channel1();
    },
    [this](byte_t) { return nr12; }
  );

  // NR13: write-only (read back as $FF)
  audio_registers[0x03].configure(
    0x00,
    [this](const byte_t value) {
      if (!apu_on_())
        return;
      nr13 = value;
    },
    [](byte_t) { return static_cast<byte_t>(0xFF); }
  );

  audio_registers[0x04].configure(
    0x00,
    [this](const byte_t value) {
      const bool prev_len_en = (nr14 & 0x40) != 0;
      const bool new_len_en = (value & 0x40) != 0;
      const bool trigger = (value & 0x80) != 0;
      if (!apu_on_())
        return;
      nr14 = value;
      // Extra length clocking: happens when the next frame-sequencer step does NOT clock length
      // On most models, this only occurs when length is transitioned 0->1; on CGB-02 it can occur even if it remains 0
      if (!next_step_clocks_length() && !prev_len_en && (new_len_en || cgb02_length_quirk_) &&
          ch1_length_counter != 0) {
        --ch1_length_counter;
        if (ch1_length_counter == 0 && !trigger)
          disable_channel1();
      }
      if (trigger)
        trigger_channel1();
    },
    [this](byte_t) { return static_cast<byte_t>(nr14 | 0xBF); }
  );

  // FF15 (NR20) is unused
  audio_registers[0x05].configure(
    0xFF, [](byte_t) {
    }, [](byte_t) { return static_cast<byte_t>(0xFF); }
  );

  // --- Channel 2 (NR21-NR24 / FF16-FF19) ---
  audio_registers[0x06].configure(
    0x00,
    [this](const byte_t value) {
      if (!apu_on_())
        return;
      nr21 = value;
      ch2_length_counter = static_cast<std::uint8_t>(64 - (value & 0x3F));
    },
    [this](byte_t) { return static_cast<byte_t>(nr21 | 0x3F); }
  );

  audio_registers[0x07].configure(
    0x00,
    [this](const byte_t value) {
      if (!apu_on_())
        return;
      nr22 = value;
      if (!ch2_dac_enabled())
        disable_channel2();
    },
    [this](byte_t) { return nr22; }
  );

  // NR23: write-only (read back as $FF)
  audio_registers[0x08].configure(
    0x00,
    [this](const byte_t value) {
      if (!apu_on_())
        return;
      nr23 = value;
    },
    [](byte_t) { return static_cast<byte_t>(0xFF); }
  );

  audio_registers[0x09].configure(
    0x00,
    [this](const byte_t value) {
      const bool prev_len_en = (nr24 & 0x40) != 0;
      const bool new_len_en = (value & 0x40) != 0;
      const bool trigger = (value & 0x80) != 0;
      if (!apu_on_())
        return;
      nr24 = value;

      // Extra length clocking
      if (!next_step_clocks_length() && !prev_len_en && (new_len_en || cgb02_length_quirk_) &&
          ch2_length_counter != 0) {
        --ch2_length_counter;
        if (ch2_length_counter == 0 && !trigger)
          disable_channel2();
      }
      if (trigger)
        trigger_channel2();
    },
    [this](byte_t) { return static_cast<byte_t>(nr24 | 0xBF); }
  );

  // --- Channel 3 (NR30-NR34 / FF1A-FF1E) ---
  audio_registers[0x0A].configure(
    0x00,
    [this](const byte_t v) {
      if (!apu_on_())
        return;
      nr30 = v;
      if (!ch3_dac_enabled())
        disable_channel3();
    },
    [this](byte_t) { return static_cast<byte_t>(nr30 | 0x7F); }
  );

  // NR31: write-only (read back as $FF)
  audio_registers[0x0B].configure(
    0x00,
    [this](const byte_t v) {
      if (!apu_on_())
        return;
      nr31 = v;
      ch3_length_counter = 256u - v;
    },
    [](byte_t) { return static_cast<byte_t>(0xFF); }
  );

  audio_registers[0x0C].configure(
    0x00,
    [this](const byte_t v) {
      if (!apu_on_())
        return;
      nr32 = v;
    },
    [this](byte_t) { return static_cast<byte_t>(nr32 | 0x9F); }
  );

  // NR33: write-only (read back as $FF)
  audio_registers[0x0D].configure(
    0x00,
    [this](const byte_t v) {
      if (!apu_on_())
        return;
      nr33 = v;
    },
    [](byte_t) { return static_cast<byte_t>(0xFF); }
  );

  audio_registers[0x0E].configure(
    0x00,
    [this](const byte_t v) {
      const bool prev_len_en = (nr34 & 0x40) != 0;
      const bool new_len_en = (v & 0x40) != 0;
      const bool trigger = (v & 0x80) != 0;
      if (!apu_on_())
        return;
      nr34 = v;

      // Extra length clocking
      if (!next_step_clocks_length() && !prev_len_en && (new_len_en || cgb02_length_quirk_) &&
          ch3_length_counter != 0) {
        --ch3_length_counter;
        if (ch3_length_counter == 0 && !trigger)
          disable_channel3();
      }
      if (trigger)
        trigger_channel3();
    },
    [this](byte_t) { return static_cast<byte_t>(nr34 | 0xBF); }
  );

  // FF1F (NR40) is unused
  audio_registers[0x0F].configure(
    0xFF, [](byte_t) {
    }, [](byte_t) { return static_cast<byte_t>(0xFF); }
  );

  // --- Channel 4 (NR41-NR44 / FF20-FF23) ---
  // NR41: write-only (read back as $FF)
  audio_registers[0x10].configure(
    0x00,
    [this](const byte_t v) {
      if (!apu_on_())
        return;
      nr41 = v;
      ch4_length_counter = static_cast<std::uint8_t>(64 - (v & 0x3F));
    },
    [](byte_t) { return static_cast<byte_t>(0xFF); }
  );

  audio_registers[0x11].configure(
    0x00,
    [this](const byte_t v) {
      if (!apu_on_())
        return;
      nr42 = v;
      if (!ch4_dac_enabled())
        disable_channel4();
    },
    [this](byte_t) { return nr42; });

  audio_registers[0x12].configure(
    0x00,
    [this](const byte_t v) {
      if (!apu_on_())
        return;
      nr43 = v;
    },
    [this](byte_t) { return nr43; }
  );

  audio_registers[0x13].configure(
    0x00,
    [this](const byte_t v) {
      const bool prev_len_en = (nr44 & 0x40) != 0;
      const bool new_len_en = (v & 0x40) != 0;
      const bool trigger = (v & 0x80) != 0;
      if (!apu_on_())
        return;
      nr44 = v;
      // Extra length clocking
      if (!next_step_clocks_length() && !prev_len_en && (new_len_en || cgb02_length_quirk_) &&
          ch4_length_counter != 0) {
        --ch4_length_counter;
        if (ch4_length_counter == 0 && !trigger)
          disable_channel4();
      }

      if (trigger)
        trigger_channel4();
    },
    [this](byte_t) { return static_cast<byte_t>(nr44 | 0xBF); }
  );

  // --- Mixer / power (NR50-NR52 / FF24-FF26) ---
  audio_registers[0x14].configure(
    power_on_nr50,
    [this](const byte_t value) {
      if (!apu_on_())
        return;
      nr50 = value;
      set_master_targets_from_nr50();
    },
    [this](byte_t) { return nr50; }
  );

  audio_registers[0x15].configure(
    power_on_nr51,
    [this](const byte_t value) {
      if (!apu_on_())
        return;
      nr51 = value;
      set_route_targets_from_nr51();
    },
    [this](byte_t) { return nr51; }
  );

  audio_registers[0x16].configure(
    power_on_nr52,
    [this](const byte_t value) {
      const bool was_on = apu_on_();

      // Only bit 7 is writable. Resets only happen on edges
      if ((value & 0x80) != 0) {
        nr52 = 0x80;
        if (!was_on) {
          // 0->1: restart sequencer step, but keep the current 8192-cycle phase
          frame_seq_step = 7;
        }
        return;
      }
      if (was_on) {
        nr52 = 0x00;
        frame_seq_step = 0;
        power_off_reset_regs_();
      }
    },
    [this](byte_t) {
      const auto status = static_cast<byte_t>(
        (channel1_enabled ? 0x01 : 0x00) | (channel2_enabled ? 0x02 : 0x00) |
        (channel3_enabled ? 0x04 : 0x00) | (channel4_enabled ? 0x08 : 0x00));
      return static_cast<byte_t>(0x70 | (nr52 & 0x80) | status);
    });
}


// ------------ Channel 1 Helpers ------------
void APU::trigger_channel1() {
  // If APU powered off, ignore triggers
  if ((nr52 & 0x80) == 0) {
    disable_channel1();
    return;
  }
  channel1_enabled = true;
  channel1_phase = 0.0;

  // Length: if zero on trigger, load max (64) (or 63 in the obscure case)
  if (ch1_length_counter == 0) {
    const bool length_enabled = (nr14 & 0x40) != 0;
    ch1_length_counter = static_cast<std::uint8_t>(
        length_enabled && !next_step_clocks_length() ? 63 : 64);
  }

  // Envelope
  ch1_env_volume = static_cast<std::uint8_t>((nr12 >> 4) & 0x0F);
  ch1_env_increase = (nr12 & 0x08) != 0;
  ch1_env_period = static_cast<std::uint8_t>(nr12 & 0x07);
  ch1_env_timer = ch1_env_period == 0 ? 8 : ch1_env_period;
  ch1_env_enabled = ch1_env_period != 0;

  // Sweep
  ch1_sweep_shadow_freq = ch1_frequency();
  ch1_sweep_period = static_cast<std::uint8_t>(nr10 >> 4 & 0x07);
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
  channel1_enabled = false;
  ch1_sweep_enabled = false;
  ch1_env_enabled = false;
}

bool APU::ch1_dac_enabled() const {
  // Channel 1 DAC enabled if any of NR12[7:3] is set
  // (If disabled, output is forced to 0 and channel is turned off)
  return (nr12 & 0xF8) != 0;
}

void APU::ch1_set_frequency(std::uint16_t freq) {
  freq &= 0x7FF;
  nr13 = static_cast<byte_t>(freq & 0xFF);
  nr14 = static_cast<byte_t>((nr14 & 0xF8) | (freq >> 8 & 0x07));
}

std::uint16_t APU::ch1_frequency() const {
  return static_cast<std::uint16_t>((nr14 & 0x07) << 8 | nr13);
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

std::uint16_t APU::ch1_sweep_calculate(bool& overflow) {
  overflow = false;
  const std::uint16_t shadow = ch1_sweep_shadow_freq;
  const auto delta =
    static_cast<std::uint16_t>(shadow >> ch1_sweep_shift);

  auto next = static_cast<std::int32_t>(shadow);
  if (ch1_sweep_negate) {
    next -= delta;
    ch1_sweep_negate_used = true;
  }
  else {
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
  // length enabled if NR14 bit 6
  if (const bool length_enabled = (nr14 & 0x40) != 0;
    !length_enabled || ch1_length_counter == 0)
    return;

  --ch1_length_counter;
  if (ch1_length_counter == 0)
    disable_channel1();
}

void APU::clock_ch1_envelope() {
  if (!ch1_env_enabled)
    return;

  if (ch1_env_timer > 0)
    --ch1_env_timer;

  if (ch1_env_timer != 0)
    return;

  ch1_env_timer = ch1_env_period == 0 ? 8 : ch1_env_period;

  if (ch1_env_increase) {
    if (ch1_env_volume < 15)
      ++ch1_env_volume;
    else
      ch1_env_enabled = false;
  }
  else {
    if (ch1_env_volume > 0)
      --ch1_env_volume;
    else
      ch1_env_enabled = false;
  }
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
  // (Blargg 04-sweep #4/#12, 05-sweep details #6)
  if (ch1_sweep_period == 0)
    return;
  // shift==0: does NOT calculate on trigger (04-sweep #3),
  // but on sweep clocks it still performs the calc/overflow-disable when period>0
  // It must NOT update channel frequency (NR13/NR14) when shift==0
  if (ch1_sweep_shift == 0) {
    bool overflow = false;
    (void)ch1_sweep_calculate(overflow); // may set negate_used if negate is active
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

// --------- Channel 2 Helpers -----------
bool APU::ch2_dac_enabled() const { return (nr22 & 0xF8) != 0; }

std::uint16_t APU::ch2_frequency() const {
  return static_cast<std::uint16_t>((nr24 & 0x07) << 8 | nr23);
}

void APU::ch2_set_frequency(std::uint16_t freq) {
  freq &= 0x7FF;
  nr23 = static_cast<byte_t>(freq & 0xFF);
  nr24 = static_cast<byte_t>((nr24 & 0xF8) | (freq >> 8 & 0x07));
}

void APU::disable_channel2() {
  channel2_enabled = false;
  ch2_env_enabled = false;
}

void APU::trigger_channel2() {
  if ((nr52 & 0x80) == 0) {
    disable_channel2();
    return;
  }
  channel2_enabled = true;
  channel2_phase = 0.0;

  if (ch2_length_counter == 0) {
    const bool length_enabled = (nr24 & 0x40) != 0;
    ch2_length_counter = static_cast<std::uint8_t>(
        length_enabled && !next_step_clocks_length() ? 63 : 64);
  }

  // Envelope (NR22)
  ch2_env_volume = static_cast<std::uint8_t>(nr22 >> 4 & 0x0F);
  ch2_env_increase = (nr22 & 0x08) != 0;
  ch2_env_period = static_cast<std::uint8_t>(nr22 & 0x07);
  ch2_env_timer = ch2_env_period == 0 ? 8 : ch2_env_period;
  ch2_env_enabled = ch2_env_period != 0;

  if (!ch2_dac_enabled())
    disable_channel2();
}

void APU::clock_ch2_length() {
  if (const bool length_enabled = (nr24 & 0x40) != 0;
    !length_enabled || ch2_length_counter == 0)
    return;

  --ch2_length_counter;
  if (ch2_length_counter == 0)
    disable_channel2();
}

void APU::clock_ch2_envelope() {
  if (!ch2_env_enabled)
    return;

  if (ch2_env_timer > 0)
    --ch2_env_timer;

  if (ch2_env_timer != 0)
    return;

  ch2_env_timer = ch2_env_period == 0 ? 8 : ch2_env_period;

  if (ch2_env_increase) {
    if (ch2_env_volume < 15)
      ++ch2_env_volume;
    else
      ch2_env_enabled = false;
  }
  else {
    if (ch2_env_volume > 0)
      --ch2_env_volume;
    else
      ch2_env_enabled = false;
  }
}

// ----------- Channel 3 Helpers -----------
void APU::clock_ch3_length() {
  if (const bool length_enabled = (nr34 & 0x40) != 0;
    !length_enabled || ch3_length_counter == 0)
    return;
  --ch3_length_counter;
  if (ch3_length_counter == 0) disable_channel3();
}

bool APU::ch3_dac_enabled() const { return (nr30 & 0x80) != 0; }

std::uint16_t APU::ch3_frequency() const {
  return static_cast<std::uint16_t>((nr34 & 0x07) << 8 | nr33);
}

void APU::disable_channel3() {
  channel3_enabled = false;
}

void APU::trigger_channel3() {
  if ((nr52 & 0x80) == 0) {
    disable_channel3();
    return;
  }
  channel3_enabled = true;
  ch3_wave_pos = 0;
  ch3_wave_byte_index = 0;
  // On trigger, CH3 outputs sample index 0 immediately, but the first timer
  // tick is delayed by an additional 3 APU cycles
  // Here: +6 T-cycles (4 MHz units)
  ch3_sample_buffer = wave_ram_bytes[0];

  const auto f = ch3_frequency();
  const auto period_tcycles = static_cast<std::uint16_t>((2048u - (f & 0x7FFu)) * 2u);
  ch3_timer = static_cast<std::uint16_t>(period_tcycles + 6u);

  if (ch3_length_counter == 0) {
    const bool length_enabled = (nr34 & 0x40) != 0;
    ch3_length_counter = static_cast<std::uint16_t>(
        length_enabled && !next_step_clocks_length() ? 255 : 256);
  }

  if (!ch3_dac_enabled())
    disable_channel3();
}

// ----------- Channel 4 Helpers -----------
void APU::clock_ch4_length() {
  if (const bool length_enabled = (nr44 & 0x40) != 0;
    !length_enabled || ch4_length_counter == 0)
    return;
  --ch4_length_counter;
  if (ch4_length_counter == 0) disable_channel4();
}

void APU::clock_ch4_envelope() {
  if (!ch4_env_enabled) return;

  if (ch4_env_timer > 0) --ch4_env_timer;
  if (ch4_env_timer != 0) return;

  ch4_env_timer = ch4_env_period == 0 ? 8 : ch4_env_period;

  if (ch4_env_increase) {
    if (ch4_env_volume < 15) ++ch4_env_volume;
    else ch4_env_enabled = false;
  }
  else {
    if (ch4_env_volume > 0) --ch4_env_volume;
    else ch4_env_enabled = false;
  }
}

bool APU::ch4_dac_enabled() const { return (nr42 & 0xF8) != 0; }

void APU::disable_channel4() {
  channel4_enabled = false;
  ch4_env_enabled = false;
}

void APU::trigger_channel4() {
  if ((nr52 & 0x80) == 0) {
    disable_channel4();
    return;
  }
  channel4_enabled = true;
  ch4_phase = 0.0;
  ch4_lfsr = 0x7FFF; // reset all 1s

  if (ch4_length_counter == 0) {
    const bool length_enabled = (nr44 & 0x40) != 0;
    ch4_length_counter = static_cast<std::uint8_t>(
        length_enabled && !next_step_clocks_length() ? 63 : 64);
  }

  // Envelope from NR42
  ch4_env_volume = nr42 >> 4 & 0x0F;
  ch4_env_increase = (nr42 & 0x08) != 0;
  ch4_env_period = nr42 & 0x07;
  ch4_env_timer = ch4_env_period == 0 ? 8 : ch4_env_period;
  ch4_env_enabled = ch4_env_period != 0;

  if (!ch4_dac_enabled())
    disable_channel4();
}

double APU::ch4_clock_hz() const {
  double r = nr43 & 0x07;
  const int s = nr43 >> 4 & 0x0F;

  if (r == 0) {
    r = 0.5;
  }
  // 262144 / r / 2^s
  return 262144.0 / r / static_cast<double>(1u << s);
}

void APU::ch4_clock_lfsr() {
  const std::uint16_t xor_bit = (ch4_lfsr ^ (ch4_lfsr >> 1)) & 0x0001;
  ch4_lfsr = (ch4_lfsr >> 1) | (xor_bit << 14);

  // width mode: also copy into bit 6 (7-bit LFSR)
  if (nr43 & 0x08) {
    ch4_lfsr = (ch4_lfsr & ~(1u << 6)) | (xor_bit << 6);
  }
}

// ------------ Sample Generation -----------
float APU::channel1_sample() const {
  if (!(nr52 & 0x80) || !channel1_enabled)
    return 0.0f;

  if (!ch1_dac_enabled())
    return 0.0f;

  const byte_t volume = ch1_env_volume;
  if (volume == 0)
    return 0.0f;

  const auto frequency = static_cast<std::uint16_t>((nr14 & 0x07) << 8 | nr13);
  if (frequency >= 2048)
    return 0.0f;

  if (const double freq_hz = 131072.0 / (2048.0 - frequency); freq_hz <= 0.0)
    return 0.0f;

  const auto duty_index = static_cast<std::uint8_t>(nr11 >> 6 & 0x03);
  const float duty = duty_table[duty_index];
  const float sample =
    (channel1_phase < duty ? 1.0f : -1.0f) * (static_cast<float>(volume) / 15.0f);
  return sample;
}

float APU::channel2_sample() const {
  if ((nr52 & 0x80) == 0 || !channel2_enabled || !ch2_dac_enabled())
    return 0.0f;

  if (const std::uint16_t frequency = ch2_frequency(); frequency >= 2048)
    return 0.0f;

  const auto duty_index = static_cast<std::uint8_t>(nr21 >> 6 & 0x03);
  const float duty = duty_table[duty_index];

  const float amp = static_cast<float>(ch2_env_volume) / 15.0f;
  if (amp <= 0.0f)
    return 0.0f;

  return (channel2_phase < duty ? 1.0f : -1.0f) * amp;
}

float APU::channel3_sample() const {
  if ((nr52 & 0x80) == 0 || !channel3_enabled || !ch3_dac_enabled())
    return 0.0f;

  // output level code
  const std::uint8_t level = nr32 >> 5 & 0x03;
  if (level == 0) return 0.0f;

  // Each wave RAM byte contains two 4-bit samples: high nibble first, then low
  const std::uint8_t raw4 = (ch3_wave_pos & 1u)
   ? static_cast<std::uint8_t>(ch3_sample_buffer & 0x0Fu)
   : static_cast<std::uint8_t>((ch3_sample_buffer >> 4) & 0x0Fu);

  // Center the 4-bit DAC output around 0, then apply the output level scaling.
  float s = (static_cast<float>(raw4) - 7.5f) / 7.5f; // ~[-1, +1]
  if (level == 2)
    s *= 0.5f;
  else if (level == 3)
    s *= 0.25f;

  return s;
}

float APU::channel4_sample() const {
  if ((nr52 & 0x80) == 0 || !channel4_enabled || !ch4_dac_enabled())
    return 0.0f;

  const float amp = static_cast<float>(ch4_env_volume) / 15.0f;
  if (amp <= 0.0f) return 0.0f;

  // Output is (inverted) bit0 in many emulator conventions:
  const bool bit0 = (ch4_lfsr & 0x01) != 0;
  const float s = bit0 ? -1.0f : 1.0f;
  return s * amp;
}

void APU::generate_sample() {
  // Smooth mixer parameters (NR50/NR51) in Reduced pop mode
  advance_mixer_smoothing();

  float ch1 = channel1_sample();
  float ch2 = channel2_sample();
  float ch3 = channel3_sample();
  float ch4 = channel4_sample();

  // When APU is powered off, the output is forced to 0 (and tails are cleared)
  if ((nr52 & 0x80) == 0) {
    ch1 = ch2 = ch3 = ch4 = 0.0f;
  }

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

  // Remove DC offset
  left = dc_block(left, dc_x1_l, dc_y1_l);
  right = dc_block(right, dc_x1_r, dc_y1_r);

  // Clamp now
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
    if (ch3_timer > 0) --ch3_timer;
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
    // CH3 phase

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
