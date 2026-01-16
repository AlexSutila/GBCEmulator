#ifndef __APU_H
#define __APU_H

#include "emu_types.hpp"
#include "memory/mmio/dmg.hpp"
#include <array>
#include <cstddef>
#include <vector>

class AddressBus;
class Frontend;

class APU {
public:
  APU(AddressBus &bus, Frontend &frontend);
  void step();

private:
  void register_mmio();

  // Ch1 helpers
  void trigger_channel1();
  void disable_channel1();
  void generate_sample();
  float channel1_sample() const;

  // APU frame sequencer
  void step_frame_sequencer();
  void clock_ch1_length();
  void clock_ch1_envelope();
  void clock_ch1_sweep();
  bool ch1_dac_enabled() const;
  std::uint16_t ch1_frequency() const;
  void ch1_set_frequency(std::uint16_t freq);
  bool ch1_sweep_overflow_check();
  std::uint16_t ch1_sweep_calculate(bool &overflow);

  static constexpr int sample_rate_hz = 48000;
  static constexpr int frames_per_buffer = 512;
  static constexpr double cpu_clock_hz = 4'194'304.0;
  static constexpr unsigned frame_sequencer_period_tcycles = 8192;

  AddressBus &bus_;
  Frontend &frontend_;
  std::array<Audio::AudioRegister, 0x17> audio_registers{};
  std::array<MMIORegister, 0x10> wave_ram{};

  std::vector<float> mix_buffer{};
  std::size_t frame_cursor{};
  double cycle_accumulator{};

  // Frame sequencer
  unsigned frame_seq_accum_tcycles{};
  std::uint8_t frame_seq_step{}; // 0..7

  byte_t nr10{};
  byte_t nr11{};
  byte_t nr12{};
  byte_t nr13{};
  byte_t nr14{};
  byte_t nr50{};
  byte_t nr51{};
  byte_t nr52{};

  // Ch 1 state
  bool channel1_enabled{};
  double channel1_phase{};

  // Length (0..64)
  std::uint8_t ch1_length_counter{};

  // Envelope
  std::uint8_t ch1_env_volume{};
  std::uint8_t ch1_env_period{};
  std::uint8_t ch1_env_timer{};
  bool ch1_env_increase{};
  bool ch1_env_enabled{};

  // Sweep
  std::uint16_t ch1_sweep_shadow_freq{};
  std::uint8_t ch1_sweep_period{};
  std::uint8_t ch1_sweep_timer{};
  std::uint8_t ch1_sweep_shift{};
  bool ch1_sweep_negate{};
  bool ch1_sweep_enabled{};
  bool ch1_sweep_negate_used{};
};

#endif // __APU_H
