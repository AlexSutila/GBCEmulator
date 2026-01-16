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

  // --- Channel 1 (NR10-NR14 / FF10-FF14) ---
  audio_registers[0x00].configure(0x00, [this](byte_t value) {
    nr10 = value;
  });

  audio_registers[0x01].configure(0x00, [this](byte_t value) {
    nr11 = value;
    // length load: 64 - N (N in low 6 bits)
    ch1_length_counter = static_cast<std::uint8_t>(64 - (value & 0x3F));
  });

  audio_registers[0x02].configure(0x00, [this](byte_t value) {
    nr12 = value;
    // If DAC is disabled, channel is forced off
    if (!ch1_dac_enabled())
      disable_channel1();
  });

  audio_registers[0x03].configure(
      0x00,
      [this](byte_t value) { nr13 = value; },
      // treat as readable shadow to keep internal freq updates visible
      [this](byte_t) { return nr13; });

  audio_registers[0x04].configure(
      0x00,
      [this](byte_t value) {
        nr14 = value;
        if (value & 0x80)
          trigger_channel1();
      },
      [this](byte_t) { return static_cast<byte_t>(nr14 & 0xBF); });

  // --- Channel 2 (NR21-NR24 / FF16-FF19) ---
  // Index mapping: FF16 - FF10 = 0x06
  audio_registers[0x06].configure(0x00, [this](byte_t value) {
    nr21 = value;
    ch2_length_counter = static_cast<std::uint8_t>(64 - (value & 0x3F));
  });

  audio_registers[0x07].configure(0x00, [this](byte_t value) {
    nr22 = value;
    if (!ch2_dac_enabled())
      disable_channel2();
  });

  audio_registers[0x08].configure(0x00, [this](byte_t value) { nr23 = value; },
                               [this](byte_t) { return nr23; });

  audio_registers[0x09].configure(
      0x00,
      [this](byte_t value) {
        nr24 = value;
        if (value & 0x80)
          trigger_channel2();
      },
      [this](byte_t) { return static_cast<byte_t>(nr24 & 0xBF); });

  // --- Channel 3 (NR30-NR34 / FF1A-FF1E) ---
  // Index mapping: FF1A - FF10 = 0x0A
  // NR30: DAC power
  audio_registers[0x0A].configure(0x00, [this](byte_t v){
    nr30 = v;
    if (!ch3_dac_enabled()) disable_channel3();
  }, [this](byte_t){ return (nr30 & 0x80) | 0x7F; });

  // NR31: length (256 - value)
  audio_registers[0x0B].configure(0x00, [this](byte_t v){
    nr31 = v;
    ch3_length_counter = 256u - v;
  });

  // NR32: output level (bits 6-5)
  audio_registers[0x0C].configure(0x00, [this](byte_t v){
    nr32 = v;
  }, [this](byte_t){ return (nr32 & 0x60) | 0x9F; });

  // NR33: freq low
  audio_registers[0x0D].configure(0x00, [this](byte_t v){ nr33 = v; },
                                 [this](byte_t){ return nr33; });

  // NR34: freq high + length enable + trigger
  audio_registers[0x0E].configure(0x00, [this](byte_t v){
    nr34 = v;
    if (v & 0x80) trigger_channel3();
  }, [this](byte_t){ return (nr34 & 0xBF); });

  // --- Mixer / power (NR50-NR52 / FF24-FF26) ---
  // Index mapping: FF24 - FF10 = 0x14
  audio_registers[0x14].configure(power_on_nr50,
                                [this](byte_t value) { nr50 = value; });
  audio_registers[0x15].configure(power_on_nr51,
                                [this](byte_t value) { nr51 = value; });
  audio_registers[0x16].configure(
      power_on_nr52,
      [this](byte_t value) {
        // Only bit 7 is writable
        const bool want_on = (value & 0x80) != 0;
        nr52 = static_cast<byte_t>(want_on ? 0x80 : 0x00);

        if (!want_on) {
          // Power off: disable channels and reset sequencer state
          disable_channel1();
          disable_channel2();
          frame_seq_accum_tcycles = 0;
          frame_seq_step = 0;
        }
      },
      [this](byte_t) {
        return static_cast<byte_t>((nr52 & 0x80) |
                                   (channel1_enabled ? 0x01 : 0x00) |
                                   (channel2_enabled ? 0x02 : 0x00) |
                                   (channel3_enabled ? 0x04 : 0x00));
      });
}


// ------------ Channel 1 Helpers ------------
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

// --------- Channel 2 Helpers -----------
bool APU::ch2_dac_enabled() const { return (nr22 & 0xF8) != 0; }

std::uint16_t APU::ch2_frequency() const {
  return static_cast<std::uint16_t>(((nr24 & 0x07) << 8) | nr23);
}

void APU::ch2_set_frequency(std::uint16_t freq) {
  freq &= 0x7FF;
  nr23 = static_cast<byte_t>(freq & 0xFF);
  nr24 = static_cast<byte_t>((nr24 & 0xF8) | ((freq >> 8) & 0x07));
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
  if (!ch2_dac_enabled()) {
    disable_channel2();
    return;
  }

  channel2_enabled = true;
  channel2_phase = 0.0;

  if (ch2_length_counter == 0)
    ch2_length_counter = 64;

  // Envelope (NR22)
  ch2_env_volume = static_cast<std::uint8_t>((nr22 >> 4) & 0x0F);
  ch2_env_increase = (nr22 & 0x08) != 0;
  ch2_env_period = static_cast<std::uint8_t>(nr22 & 0x07);
  ch2_env_timer = (ch2_env_period == 0) ? 8 : ch2_env_period;
  ch2_env_enabled = (ch2_env_period != 0);
}

void APU::clock_ch2_length() {
  const bool length_enabled = (nr24 & 0x40) != 0;
  if (!length_enabled || ch2_length_counter == 0)
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

  ch2_env_timer = (ch2_env_period == 0) ? 8 : ch2_env_period;

  if (ch2_env_increase) {
    if (ch2_env_volume < 15)
      ++ch2_env_volume;
    else
      ch2_env_enabled = false;
  } else {
    if (ch2_env_volume > 0)
      --ch2_env_volume;
    else
      ch2_env_enabled = false;
  }
}

// ----------- Channel 3 Helpers -----------
void APU::clock_ch3_length() {
  const bool length_enabled = (nr34 & 0x40) != 0;
  if (!length_enabled || ch3_length_counter == 0) return;
  --ch3_length_counter;
  if (ch3_length_counter == 0) disable_channel3();
}

bool APU::ch3_dac_enabled() const { return (nr30 & 0x80) != 0; }

std::uint16_t APU::ch3_frequency() const {
  return std::uint16_t(((nr34 & 0x07) << 8) | nr33);
}

void APU::disable_channel3() { channel3_enabled = false; }

void APU::trigger_channel3() {
  if ((nr52 & 0x80) == 0) { disable_channel3(); return; }
  if (!ch3_dac_enabled()) { disable_channel3(); return; }

  channel3_enabled = true;
  channel3_pos = 0.0;

  if (ch3_length_counter == 0) ch3_length_counter = 256;
}

// ------------ Sample Generation -----------
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

float APU::channel2_sample() const {
  if ((nr52 & 0x80) == 0 || !channel2_enabled || !ch2_dac_enabled())
    return 0.0f;

  const std::uint16_t frequency = ch2_frequency();
  if (frequency >= 2048)
    return 0.0f;

  const std::uint8_t duty_index = static_cast<std::uint8_t>((nr21 >> 6) & 0x03);
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
  const std::uint8_t level = (nr32 >> 5) & 0x03;
  if (level == 0) return 0.0f;

  const int idx = int(channel3_pos) & 31;
  const byte_t b = wave_ram[idx >> 1].peek();
  std::uint8_t sample4 = (idx & 1) ? (b & 0x0F) : (b >> 4);

  // level shift
  if (level == 2) sample4 >>= 1;
  else if (level == 3) sample4 >>= 2;

  // center around 0
  const float s = (float(sample4) - 8.0f) / 8.0f; // ~[-1, +0.875]
  return s;
}


void APU::generate_sample() {
  const float ch1 = channel1_sample();
  const float ch2 = channel2_sample();
  const float ch3 = channel3_sample();

  const float master_left = ((nr50 >> 4) & 0x07) / 7.0f;
  const float master_right = (nr50 & 0x07) / 7.0f;

  // NR51 routing:
  // Right: bit0=CH1, bit1=CH2, bit2=CH3, bit3=CH4
  // Left : bit4=CH1, bit5=CH2, bit6=CH3, bit7=CH4
  float left_raw = 0.0f;
  float right_raw = 0.0f;

  if (nr51 & 0x10)
    left_raw += ch1;
  if (nr51 & 0x20)
    left_raw += ch2;
  if (nr51 & 0x40)
    left_raw += ch3;

  if (nr51 & 0x01)
    right_raw += ch1;
  if (nr51 & 0x02)
    right_raw += ch2;
  if (nr51 & 0x04)
    right_raw += ch3;

  float left = left_raw * master_left;
  float right = right_raw * master_right;

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

void APU::step_frame_sequencer() {
  if ((nr52 & 0x80) == 0)
    return;

  frame_seq_accum_tcycles += 1;
  while (frame_seq_accum_tcycles >= frame_sequencer_period_tcycles) {
    frame_seq_accum_tcycles -= frame_sequencer_period_tcycles;
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
      break;
    default:
      break;
    }

    if (frame_seq_step == 2 || frame_seq_step == 6)
      clock_ch1_sweep();

    if (frame_seq_step == 7) {
      clock_ch1_envelope();
      clock_ch2_envelope();
    }
  }
}


void APU::step() {
  step_frame_sequencer();

  constexpr double cycles_per_sample = cpu_clock_hz / sample_rate_hz;
  cycle_accumulator += 1.0;
  if (cycle_accumulator < cycles_per_sample)
    return;

  cycle_accumulator -= cycles_per_sample;

  if ((nr52 & 0x80) != 0) {
    // CH1 phase
    if (channel1_enabled) {
      const std::uint16_t f = ch1_frequency();
      if (f < 2048) {
        const double hz = 131072.0 / (2048.0 - f);
        channel1_phase += hz / sample_rate_hz;
        if (channel1_phase >= 1.0)
          channel1_phase -= 1.0;
      }
    }

    // CH2 phase
    if (channel2_enabled) {
      const std::uint16_t f = ch2_frequency();
      if (f < 2048) {
        const double hz = 131072.0 / (2048.0 - f);
        channel2_phase += hz / sample_rate_hz;
        if (channel2_phase >= 1.0)
          channel2_phase -= 1.0;
      }
    }
    // CH3 phase
    if (channel3_enabled) {
      const auto f = ch3_frequency();
      if (f < 2048) {
        // CH3 sample-step rate: 2097152 / (2048 - f) steps/sec
        const double step_hz = 2097152.0 / (2048.0 - f);
        channel3_pos += step_hz / sample_rate_hz;
        while (channel3_pos >= 32.0) channel3_pos -= 32.0;
      }
    }
  }

  generate_sample();
}
