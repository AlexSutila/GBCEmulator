#include "apu/apu.hpp"
#include "frontend/frontend.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/mmio.hpp"
#include <algorithm>
#include <cmath>

namespace {
  constexpr addr_t audio_base =
      static_cast<addr_t>(IORegisterMapping::MMIO_AUDIO_BASE);
  constexpr addr_t wave_ram_base =
      static_cast<addr_t>(IORegisterMapping::MMIO_WAVE_RAM_BASE);
constexpr std::size_t audio_register_count = 0x17;
constexpr std::size_t wave_ram_size = 0x10;
constexpr float master_gain = 0.25f;
constexpr byte_t power_on_nr50 = 0x77;
constexpr byte_t power_on_nr51 = 0xF3;
constexpr byte_t power_on_nr52 = 0x80;

constexpr std::array<float, 4> duty_table{0.125f, 0.25f, 0.5f, 0.75f};

float clamp_sample(float v) {
  return std::clamp(v, -1.0f, 1.0f);
}
} // namespace

void Audio::AudioRegister::configure(byte_t initial, WriteCallback on_write_cb,
                                   ReadCallback on_read_cb) {
  state = initial;
  on_write = std::move(on_write_cb);
  on_read = std::move(on_read_cb);
}

void Audio::AudioRegister::write(byte_t value) {
  state = value;
  if (on_write)
    on_write(value);
}

byte_t Audio::AudioRegister::read() {
  if (on_read)
    return on_read(state);
  return state;
}

APU::APU(AddressBus &bus, Frontend &frontend)
    : bus_(bus), frontend_(frontend) {
  mix_buffer.resize(frames_per_buffer * 2);
  register_mmio();
}

void APU::register_mmio() {
  nr50 = power_on_nr50;
  nr51 = power_on_nr51;
  nr52 = power_on_nr52;

  for (std::size_t i = 0; i < audio_register_count; ++i)
    bus_.connect_mmio(static_cast<addr_t>(audio_base + i),
                      &audio_registers[i]);
  for (std::size_t i = 0; i < wave_ram_size; ++i)
    bus_.connect_mmio(static_cast<addr_t>(wave_ram_base + i), &wave_ram[i]);

  // --- Channel 1 ---
  audio_registers[0].configure(0x00, [this](byte_t value) {
    nr10 = value;
  });

  audio_registers[1].configure(0x00, [this](byte_t value) {
    nr11 = value;
    // length load: 64 - N (N in low 6 bits)
    ch1_length_counter = static_cast<std::uint8_t>(64 - (value & 0x3F));
  });

  audio_registers[2].configure(0x00, [this](byte_t value) {
    nr12 = value;
    // If DAC is disabled, channel is forced off
    if (!ch1_dac_enabled())
      disable_channel1();
  });

  audio_registers[3].configure(
      0x00,
      [this](byte_t value) { nr13 = value; },
      // treat as readable shadow to keep internal freq updates visible
      [this](byte_t) { return nr13; });

  audio_registers[4].configure(
      0x00,
      [this](byte_t value) {
        nr14 = value;
        if (value & 0x80)
          trigger_channel1();
      },
      [this](byte_t) { return static_cast<byte_t>(nr14 & 0xBF); });

  // --- Mixer / power ---
  audio_registers[20].configure(power_on_nr50,
                                [this](byte_t value) { nr50 = value; });
  audio_registers[21].configure(power_on_nr51,
                                [this](byte_t value) { nr51 = value; });
  audio_registers[22].configure(
      power_on_nr52,
      [this](byte_t value) {
        // Only bit 7 is writable
        const bool want_on = (value & 0x80) != 0;
        nr52 = static_cast<byte_t>(want_on ? 0x80 : 0x00);

        if (!want_on) {
          // Power off: disable channels and reset sequencer state
          disable_channel1();
          frame_seq_accum_tcycles = 0;
          frame_seq_step = 0;
        }
      },
      [this](byte_t) {
        return static_cast<byte_t>((nr52 & 0x80) |
                                   (channel1_enabled ? 0x01 : 0x00));
      });
}

void APU::trigger_channel1() {
  // If APU powered off, ignore triggers
  if ((nr52 & 0x80) == 0) {
    disable_channel1();
    return;
  }

  // If DAC disabled, hardware immediately disables the channel
  if (!ch1_dac_enabled()) {
    disable_channel1();
    return;
  }

  channel1_enabled = true;
  channel1_phase = 0.0;

  // Length: if zero on trigger, load max (64)
  if (ch1_length_counter == 0)
    ch1_length_counter = 64;

  // Envelope
  ch1_env_volume = static_cast<std::uint8_t>((nr12 >> 4) & 0x0F);
  ch1_env_increase = (nr12 & 0x08) != 0;
  ch1_env_period = static_cast<std::uint8_t>(nr12 & 0x07);
  ch1_env_timer = (ch1_env_period == 0) ? 8 : ch1_env_period;
  ch1_env_enabled = (ch1_env_period != 0);

  // Sweep
  ch1_sweep_shadow_freq = ch1_frequency();
  ch1_sweep_period = static_cast<std::uint8_t>((nr10 >> 4) & 0x07);
  ch1_sweep_shift = static_cast<std::uint8_t>(nr10 & 0x07);
  ch1_sweep_negate = (nr10 & 0x08) != 0;
  ch1_sweep_timer = (ch1_sweep_period == 0) ? 8 : ch1_sweep_period;
  ch1_sweep_enabled = (ch1_sweep_period != 0) || (ch1_sweep_shift != 0);
  ch1_sweep_negate_used = false;

  // Overflow check on trigger
  (void)ch1_sweep_overflow_check();
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
  nr14 = static_cast<byte_t>((nr14 & 0xF8) | ((freq >> 8) & 0x07));
}

float APU::channel1_sample() const {
  if (!(nr52 & 0x80) || !channel1_enabled)
    return 0.0f;

  if (!ch1_dac_enabled())
    return 0.0f;

  const byte_t volume = static_cast<byte_t>((nr12 >> 4) & 0x0F);
  if (volume == 0)
    return 0.0f;

  const std::uint16_t frequency =
      static_cast<std::uint16_t>(((nr14 & 0x07) << 8) | nr13);
  if (frequency >= 2048)
    return 0.0f;

  const double freq_hz = 131072.0 / (2048.0 - frequency);
  if (freq_hz <= 0.0)
    return 0.0f;

  const std::uint8_t duty_index = static_cast<std::uint8_t>((nr11 >> 6) & 0x03);
  const float duty = duty_table[duty_index];
  const float sample =
      (channel1_phase < duty ? 1.0f : -1.0f) * (volume / 15.0f);
  return sample;
}

void APU::generate_sample() {
  const float raw = channel1_sample();

  const float master_left = ((nr50 >> 4) & 0x07) / 7.0f;
  const float master_right = (nr50 & 0x07) / 7.0f;

  const bool left_enable = (nr51 & 0x10) != 0;
  const bool right_enable = (nr51 & 0x01) != 0;

  float left = left_enable ? raw * master_left : 0.0f;
  float right = right_enable ? raw * master_right : 0.0f;

  left = clamp_sample(left * master_gain);
  right = clamp_sample(right * master_gain);

  mix_buffer[frame_cursor * 2] = left;
  mix_buffer[frame_cursor * 2 + 1] = right;
  ++frame_cursor;

  if (frame_cursor >= frames_per_buffer) {
    frontend_.queue_audio_samples(mix_buffer.data(), mix_buffer.size());
    frame_cursor = 0;
  }
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
  const std::uint16_t delta =
      static_cast<std::uint16_t>(shadow >> ch1_sweep_shift);

  std::int32_t next = static_cast<std::int32_t>(shadow);
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
  // length enabled if NR14 bit 6
  const bool length_enabled = (nr14 & 0x40) != 0;
  if (!length_enabled || ch1_length_counter == 0)
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

  ch1_env_timer = (ch1_env_period == 0) ? 8 : ch1_env_period;

  if (ch1_env_increase) {
    if (ch1_env_volume < 15)
      ++ch1_env_volume;
    else
      ch1_env_enabled = false;
  } else {
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

  ch1_sweep_timer = (ch1_sweep_period == 0) ? 8 : ch1_sweep_period;

  if (ch1_sweep_shift == 0)
    return;

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

void APU::step_frame_sequencer() {
  if ((nr52 & 0x80) == 0)
    return;

  frame_seq_accum_tcycles += 1;
  while (frame_seq_accum_tcycles >= frame_sequencer_period_tcycles) {
    frame_seq_accum_tcycles -= frame_sequencer_period_tcycles;
    frame_seq_step = static_cast<std::uint8_t>((frame_seq_step + 1) & 0x07);

    // Frame sequencer schedule:
    // 0,2,4,6: length
    // 2,6: sweep
    // 7: envelope
    switch (frame_seq_step) {
    case 0:
    case 2:
    case 4:
    case 6:
      clock_ch1_length();
      break;
    default:
      break;
    }

    if (frame_seq_step == 2 || frame_seq_step == 6)
      clock_ch1_sweep();

    if (frame_seq_step == 7)
      clock_ch1_envelope();
  }
}


void APU::step() {
  step_frame_sequencer();

  constexpr double cycles_per_sample = cpu_clock_hz / sample_rate_hz;
  cycle_accumulator += 1.0;
  if (cycle_accumulator < cycles_per_sample)
    return;

  cycle_accumulator -= cycles_per_sample;

  const std::uint16_t frequency = ch1_frequency();
  if (frequency < 2048) {
    const double freq_hz = 131072.0 / (2048.0 - frequency);
    channel1_phase += freq_hz / sample_rate_hz;
    if (channel1_phase >= 1.0)
      channel1_phase -= 1.0;
  }

  generate_sample();
}
