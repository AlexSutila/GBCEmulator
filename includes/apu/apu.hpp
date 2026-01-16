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
  void generate_sample();

  // Ch1 helpers
  void trigger_channel1();
  void disable_channel1();
  float channel1_sample() const;
  // Ch2 helpers
  void trigger_channel2();
  void disable_channel2();
  float channel2_sample() const;
  // Ch3 helpers
  void trigger_channel3();
  void disable_channel3();
  float channel3_sample() const;

  // APU frame sequencer
  void step_frame_sequencer();
  // --> Ch1
  void clock_ch1_length();
  void clock_ch1_envelope();
  void clock_ch1_sweep();
  bool ch1_dac_enabled() const;
  std::uint16_t ch1_frequency() const;
  void ch1_set_frequency(std::uint16_t freq);
  bool ch1_sweep_overflow_check();
  std::uint16_t ch1_sweep_calculate(bool &overflow);
  // --> Ch2
  void clock_ch2_length();
  void clock_ch2_envelope();
  bool ch2_dac_enabled() const;
  std::uint16_t ch2_frequency() const;
  void ch2_set_frequency(std::uint16_t freq);
  // --> Ch3
  void clock_ch3_length();
  std::uint16_t ch3_frequency() const;
  void ch3_set_frequency(std::uint16_t f);
  bool ch3_dac_enabled() const;

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

  // Internal audio registers
  // NR10-NR14: Channel 1
  byte_t nr10{};
  byte_t nr11{};
  byte_t nr12{};
  byte_t nr13{};
  byte_t nr14{};
  // NR20-NR24: Channel 2
  byte_t nr21{};
  byte_t nr22{};
  byte_t nr23{};
  byte_t nr24{};
  // NR30-NR34: Channel 3
  byte_t nr30{};
  byte_t nr31{};
  byte_t nr32{};
  byte_t nr33{};
  byte_t nr34{};
  // NR50-NR52: Control
  byte_t nr50{};
  byte_t nr51{};
  byte_t nr52{};

  // Channel state
  bool channel1_enabled{};
  double channel1_phase{};
  bool channel2_enabled{};
  double channel2_phase{};
  bool channel3_enabled{};
  double channel3_pos{};    // 0..32

  // Length (0..64)
  std::uint8_t ch1_length_counter{};
  std::uint8_t ch2_length_counter{};
  std::uint16_t ch3_length_counter{}; // 0..256

  // Envelope
  // Ch1
  std::uint8_t ch1_env_volume{};
  std::uint8_t ch1_env_period{};
  std::uint8_t ch1_env_timer{};
  bool ch1_env_increase{};
  bool ch1_env_enabled{};
  // Ch2
  std::uint8_t ch2_env_volume{};
  std::uint8_t ch2_env_period{};
  std::uint8_t ch2_env_timer{};
  bool ch2_env_increase{};
  bool ch2_env_enabled{};

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
