#include "apu/apu.hpp"
#include "frontend/frontend.hpp"
#include "memory/bus.hpp"
#include <algorithm>
#include <cmath>

namespace {
constexpr addr_t audio_base = 0xFF10;
constexpr addr_t wave_ram_base = 0xFF30;
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

void APU::AudioRegister::configure(byte_t initial, WriteCallback on_write_cb,
                                   ReadCallback on_read_cb) {
  state = initial;
  on_write = std::move(on_write_cb);
  on_read = std::move(on_read_cb);
}

void APU::AudioRegister::write(byte_t value) {
  state = value;
  if (on_write)
    on_write(value);
}

byte_t APU::AudioRegister::read() {
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

  audio_registers[0].configure(0x00, [this](byte_t value) { nr10 = value; });
  audio_registers[1].configure(0x00, [this](byte_t value) { nr11 = value; });
  audio_registers[2].configure(0x00, [this](byte_t value) { nr12 = value; });
  audio_registers[3].configure(0x00, [this](byte_t value) { nr13 = value; });
  audio_registers[4].configure(
      0x00,
      [this](byte_t value) {
        nr14 = value;
        if (value & 0x80)
          trigger_channel1();
      },
      [this](byte_t) {
        return static_cast<byte_t>(nr14 & 0xBF);
      });

  audio_registers[20].configure(power_on_nr50,
                                [this](byte_t value) { nr50 = value; });
  audio_registers[21].configure(power_on_nr51,
                                [this](byte_t value) { nr51 = value; });
  audio_registers[22].configure(
      power_on_nr52,
      [this](byte_t value) {
        nr52 = static_cast<byte_t>(value & 0x80);
        if ((nr52 & 0x80) == 0)
          channel1_enabled = false;
      },
      [this](byte_t) {
        return static_cast<byte_t>((nr52 & 0x80) |
                                   (channel1_enabled ? 0x01 : 0x00));
      });
}

void APU::trigger_channel1() {
  channel1_enabled = (nr52 & 0x80) != 0;
  channel1_phase = 0.0;
}

float APU::channel1_sample() const {
  if (!(nr52 & 0x80) || !channel1_enabled)
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

void APU::step() {
  const double cycles_per_sample = cpu_clock_hz / sample_rate_hz;
  cycle_accumulator += 1.0;
  if (cycle_accumulator < cycles_per_sample)
    return;

  cycle_accumulator -= cycles_per_sample;

  const std::uint16_t frequency =
      static_cast<std::uint16_t>(((nr14 & 0x07) << 8) | nr13);
  if (frequency < 2048) {
    const double freq_hz = 131072.0 / (2048.0 - frequency);
    channel1_phase += freq_hz / sample_rate_hz;
    if (channel1_phase >= 1.0)
      channel1_phase -= 1.0;
  }

  generate_sample();
}
