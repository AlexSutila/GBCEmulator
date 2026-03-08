#include "apu/apu.hpp"
#include "apu/apu_helpers.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/mmio.hpp"

constexpr addr_t audio_base =
    static_cast<addr_t>(IORegisterMapping::MMIO_AUDIO_BASE);
constexpr std::size_t audio_register_count = 0x17;
constexpr std::size_t audio_unused_count = 0x09; // FF27-FF2F
constexpr std::size_t wave_ram_size = 0x10;

[[nodiscard]] byte_t read_ff(byte_t) { return 0xFF; }

void Audio::AudioRegister::configure(const byte_t initial,
                                     WriteCallback on_write_cb,
                                     ReadCallback on_read_cb) {
  state_ = initial;
  on_write = std::move(on_write_cb);
  on_read = std::move(on_read_cb);
}

void Audio::AudioRegister::write(const byte_t value) {
  state_ = value;
  if (on_write)
    on_write(value);
}

byte_t Audio::AudioRegister::read() {
  if (on_read)
    return on_read(state_);
  return state_;
}

void APU::power_off_reset_regs_() {
  // When NR52 is turned off, hardware clears all APU regs (wave RAM unaffected)
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

void APU::register_mmio() {
  nr50 = power_on_nr50;
  nr51 = power_on_nr51;
  nr52 = power_on_nr52;

  for (std::size_t i = 0; i < audio_register_count; ++i)
    bus_.connect_mmio(static_cast<addr_t>(audio_base + i), &audio_registers[i]);
  for (std::size_t i = 0; i < audio_unused_count; ++i)
    bus_.connect_mmio(
        static_cast<addr_t>(audio_base + audio_register_count + i),
        &audio_unused[i]);
  for (std::size_t i = 0; i < wave_ram_size; ++i) {
    bus_.connect_mmio(static_cast<addr_t>(audio_base + audio_register_count +
                                          audio_unused_count + i),
                      &wave_ram[i]);
  }

  for (auto &b : wave_ram_bytes)
    b = 0;

  // Wave RAM is accessible even when NR52 is off
  // On CGB, while CH3 is playing, accesses are redirected to the byte selected
  // by the current waveform position
  for (std::size_t i = 0; i < wave_ram_size; ++i) {
    wave_ram[i].configure(
        0x00,
        [this, i](const byte_t v) {
          const std::size_t dst =
              channel3_enabled
                  ? static_cast<std::size_t>((ch3_wave_pos >> 1) & 0x0Fu)
                  : i;
          wave_ram_bytes[dst] = v;
        },
        [this, i](byte_t) -> byte_t {
          const std::size_t src =
              channel3_enabled
                  ? static_cast<std::size_t>((ch3_wave_pos >> 1) & 0x0Fu)
                  : i;
          return wave_ram_bytes[src];
        });
  }

  // Unused audio area FF27-FF2F reads back as $FF and ignores writes
  for (auto &r : audio_unused)
    r.configure(0xFF, [](byte_t) {}, read_ff);

  // --- Channel 1 (NR10-NR14 / FF10-FF14) ---
  audio_registers[0x00].configure(
      0x00,
      [this](const byte_t value) {
        if (!apu_on_())
          return;

        const byte_t old = nr10;
        nr10 = value;

        // Sweep negate quirk: if negation was used, clearing negate disables
        // CH1
        if (channel1_enabled) {
          const bool old_neg = (old & 0x08) != 0;
          const bool new_neg = (value & 0x08) != 0;
          if (ch1_sweep_negate_used && old_neg && !new_neg) {
            disable_channel1();
            return;
          }
        }

        // Sweep enabled is latched on trigger; NR10 writes can't enable it
        // later
        if (ch1_sweep_enabled) {
          ch1_sweep_period = static_cast<std::uint8_t>((nr10 >> 4) & 0x07);
          ch1_sweep_shift = static_cast<std::uint8_t>(nr10 & 0x07);
          ch1_sweep_negate = (nr10 & 0x08) != 0;
        }
      },
      [this](byte_t) { return static_cast<byte_t>(nr10 | 0x80); });

  audio_registers[0x01].configure(
      0x00,
      [this](const byte_t value) {
        if (!apu_on_())
          return;
        nr11 = value;
        ch1_length_counter = static_cast<std::uint8_t>(64 - (value & 0x3F));
      },
      [this](byte_t) { return static_cast<byte_t>(nr11 | 0x3F); });

  audio_registers[0x02].configure(
      0x00,
      [this](const byte_t value) {
        if (!apu_on_())
          return;
        nr12 = value;
        if (!ch1_dac_enabled())
          disable_channel1();
      },
      [this](byte_t) { return nr12; });

  // NR13: write-only (read back as $FF)
  audio_registers[0x03].configure(
      0x00,
      [this](const byte_t value) {
        if (!apu_on_())
          return;
        nr13 = value;
      },
      read_ff);

  audio_registers[0x04].configure(
      0x00,
      [this](const byte_t value) {
        const bool prev_len_en = (nr14 & 0x40) != 0;
        const bool new_len_en = (value & 0x40) != 0;
        const bool trigger = (value & 0x80) != 0;

        if (!apu_on_())
          return;
        nr14 = value;

        maybe_extra_length_clock(next_step_clocks_length(), prev_len_en,
                                 new_len_en, cgb02_length_quirk_,
                                 ch1_length_counter, trigger,
                                 [this] { disable_channel1(); });

        if (trigger)
          trigger_channel1();
      },
      [this](byte_t) { return static_cast<byte_t>(nr14 | 0xBF); });

  // FF15 (NR20) is unused.
  audio_registers[0x05].configure(0xFF, [](byte_t) {}, read_ff);

  // --- Channel 2 (NR21-NR24 / FF16-FF19) ---
  audio_registers[0x06].configure(
      0x00,
      [this](const byte_t value) {
        if (!apu_on_())
          return;
        nr21 = value;
        ch2_length_counter = static_cast<std::uint8_t>(64 - (value & 0x3F));
      },
      [this](byte_t) { return static_cast<byte_t>(nr21 | 0x3F); });

  audio_registers[0x07].configure(
      0x00,
      [this](const byte_t value) {
        if (!apu_on_())
          return;
        nr22 = value;
        if (!ch2_dac_enabled())
          disable_channel2();
      },
      [this](byte_t) { return nr22; });

  // NR23: write-only (read back as $FF)
  audio_registers[0x08].configure(
      0x00,
      [this](const byte_t value) {
        if (!apu_on_())
          return;
        nr23 = value;
      },
      read_ff);

  audio_registers[0x09].configure(
      0x00,
      [this](const byte_t value) {
        const bool prev_len_en = (nr24 & 0x40) != 0;
        const bool new_len_en = (value & 0x40) != 0;
        const bool trigger = (value & 0x80) != 0;

        if (!apu_on_())
          return;
        nr24 = value;

        maybe_extra_length_clock(next_step_clocks_length(), prev_len_en,
                                 new_len_en, cgb02_length_quirk_,
                                 ch2_length_counter, trigger,
                                 [this] { disable_channel2(); });

        if (trigger)
          trigger_channel2();
      },
      [this](byte_t) { return static_cast<byte_t>(nr24 | 0xBF); });

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
      [this](byte_t) { return static_cast<byte_t>(nr30 | 0x7F); });

  // NR31: write-only (read back as $FF)
  audio_registers[0x0B].configure(
      0x00,
      [this](const byte_t v) {
        if (!apu_on_())
          return;
        nr31 = v;
        ch3_length_counter = 256u - v;
      },
      read_ff);

  audio_registers[0x0C].configure(
      0x00,
      [this](const byte_t v) {
        if (!apu_on_())
          return;
        nr32 = v;
      },
      [this](byte_t) { return static_cast<byte_t>(nr32 | 0x9F); });

  // NR33: write-only (read back as $FF)
  audio_registers[0x0D].configure(
      0x00,
      [this](const byte_t v) {
        if (!apu_on_())
          return;
        nr33 = v;
      },
      read_ff);

  audio_registers[0x0E].configure(
      0x00,
      [this](const byte_t v) {
        const bool prev_len_en = (nr34 & 0x40) != 0;
        const bool new_len_en = (v & 0x40) != 0;
        const bool trigger = (v & 0x80) != 0;

        if (!apu_on_())
          return;
        nr34 = v;

        maybe_extra_length_clock(next_step_clocks_length(), prev_len_en,
                                 new_len_en, cgb02_length_quirk_,
                                 ch3_length_counter, trigger,
                                 [this] { disable_channel3(); });

        if (trigger)
          trigger_channel3();
      },
      [this](byte_t) { return static_cast<byte_t>(nr34 | 0xBF); });

  // FF1F (NR40) is unused.
  audio_registers[0x0F].configure(0xFF, [](byte_t) {}, read_ff);

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
      read_ff);

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
      [this](byte_t) { return nr43; });

  audio_registers[0x13].configure(
      0x00,
      [this](const byte_t v) {
        const bool prev_len_en = (nr44 & 0x40) != 0;
        const bool new_len_en = (v & 0x40) != 0;
        const bool trigger = (v & 0x80) != 0;

        if (!apu_on_())
          return;
        nr44 = v;

        maybe_extra_length_clock(next_step_clocks_length(), prev_len_en,
                                 new_len_en, cgb02_length_quirk_,
                                 ch4_length_counter, trigger,
                                 [this] { disable_channel4(); });

        if (trigger)
          trigger_channel4();
      },
      [this](byte_t) { return static_cast<byte_t>(nr44 | 0xBF); });

  // --- Mixer / power (NR50-NR52 / FF24-FF26) ---
  audio_registers[0x14].configure(
      power_on_nr50,
      [this](const byte_t value) {
        if (!apu_on_())
          return;
        nr50 = value;
        set_master_targets_from_nr50();
      },
      [this](byte_t) { return nr50; });

  audio_registers[0x15].configure(
      power_on_nr51,
      [this](const byte_t value) {
        if (!apu_on_())
          return;
        nr51 = value;
        set_route_targets_from_nr51();
      },
      [this](byte_t) { return nr51; });

  audio_registers[0x16].configure(
      power_on_nr52,
      [this](const byte_t value) {
        const bool was_on = apu_on_();

        // Only bit 7 is writable. Resets only happen on edges
        if ((value & 0x80) != 0) {
          nr52 = 0x80;
          if (!was_on) {
            // 0->1: restart sequencer step, but keep the current 8192-cycle
            // phase
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
        const auto status =
            static_cast<byte_t>((channel1_enabled ? 0x01 : 0x00) |
                                (channel2_enabled ? 0x02 : 0x00) |
                                (channel3_enabled ? 0x04 : 0x00) |
                                (channel4_enabled ? 0x08 : 0x00));
        return static_cast<byte_t>(0x70 | (nr52 & 0x80) | status);
      });
}
