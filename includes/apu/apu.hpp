#ifndef GBC_APU_HPP
#define GBC_APU_HPP

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

  // Enable the CGB-02 extra-length-clocking quirk (default: off)
  // When disabled, extra length clocking only happens on a 0->1 transition of
  // NRx4 bit 6
  void set_cgb02_length_quirk(const bool enable) { cgb02_length_quirk_ = enable; }

private:
  [[nodiscard]] bool apu_on_() const { return (nr52 & 0x80) != 0; }
  void power_off_reset_regs_();

  void register_mmio();
  void generate_sample();

  void sync_mixer_targets_from_regs();
  void set_master_targets_from_nr50();
  void set_route_targets_from_nr51();
  void advance_mixer_smoothing();

  // Ch1 helpers
  void trigger_channel1();
  void disable_channel1();
  [[nodiscard]] float channel1_sample() const;
  // Ch2 helpers
  void trigger_channel2();
  void disable_channel2();
  [[nodiscard]] float channel2_sample() const;
  // Ch3 helpers
  void trigger_channel3();
  void disable_channel3();
  [[nodiscard]] float channel3_sample() const;
  // Ch4 helpers
  void trigger_channel4();
  void disable_channel4();
  [[nodiscard]] float channel4_sample() const;

  // APU frame sequencer
  void step_frame_sequencer();
  // Used for obscure length-counter behavior (Blargg cgb_sound 03-trigger)
  [[nodiscard]] bool next_step_clocks_length() const;
  // --> Ch1
  void clock_ch1_length();
  void clock_ch1_envelope();
  void clock_ch1_sweep();
  [[nodiscard]] bool ch1_dac_enabled() const;
  [[nodiscard]] std::uint16_t ch1_frequency() const;
  void ch1_set_frequency(std::uint16_t freq);
  bool ch1_sweep_overflow_check();
  std::uint16_t ch1_sweep_calculate(bool &overflow);
  // --> Ch2
  void clock_ch2_length();
  void clock_ch2_envelope();
  [[nodiscard]] bool ch2_dac_enabled() const;
  [[nodiscard]] std::uint16_t ch2_frequency() const;
  void ch2_set_frequency(std::uint16_t freq);
  // --> Ch3
  void clock_ch3_length();
  [[nodiscard]] std::uint16_t ch3_frequency() const;
  [[nodiscard]] bool ch3_dac_enabled() const;
  // --> Ch4
  void clock_ch4_length();
  void clock_ch4_envelope();
  [[nodiscard]] bool ch4_dac_enabled() const;
  [[nodiscard]] double ch4_clock_hz() const;
  void ch4_clock_lfsr();

  static constexpr int sample_rate_hz = 48000;
  static constexpr int frames_per_buffer = 128;
  static constexpr double cpu_clock_hz = 4'194'304.0;
  static constexpr unsigned frame_sequencer_period_tcycles = 8192;

  AddressBus &bus_;
  Frontend &frontend_;
  std::array<Audio::AudioRegister, 0x17> audio_registers{};
  std::array<Audio::AudioRegister, 0x09> audio_unused{}; // FF27-FF2F
  std::array<Audio::AudioRegister, 0x10> wave_ram{};
  std::array<byte_t, 0x10> wave_ram_bytes{};

  std::vector<float> mix_buffer{};
  std::size_t frame_cursor{};
  double cycle_accumulator{};

  // Frame sequencer
  unsigned frame_seq_accum_tcycles{};
  std::uint8_t frame_seq_step{}; // 0..7
  bool cgb02_length_quirk_{};

  // Shadow audio registers
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
  // NR40-NR44: Channel 4
  byte_t nr41{};
  byte_t nr42{};
  byte_t nr43{};
  byte_t nr44{};
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
  std::uint8_t ch3_wave_pos{};        // 0..31 (4-bit samples)
  std::uint16_t ch3_timer{};          // t-cycles until next sample step
  std::uint8_t ch3_wave_byte_index{}; // 0..15, last wave RAM byte read by CH3
  byte_t ch3_sample_buffer{};         // last byte fetched from wave RAM (persists
                                      // across retriggers)
  bool channel4_enabled{};
  double ch4_phase{}; // fractional clocks accumulator

  // Length (0..64)
  std::uint8_t ch1_length_counter{};
  std::uint8_t ch2_length_counter{};
  std::uint16_t ch3_length_counter{}; // 0..256
  std::uint8_t ch4_length_counter{};

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
  // Ch4
  std::uint8_t ch4_env_volume{};
  std::uint8_t ch4_env_period{};
  std::uint8_t ch4_env_timer{};
  bool ch4_env_increase{};
  bool ch4_env_enabled{};

  // Sweep
  std::uint16_t ch1_sweep_shadow_freq{};
  std::uint8_t ch1_sweep_period{};
  std::uint8_t ch1_sweep_timer{};
  std::uint8_t ch1_sweep_shift{};
  bool ch1_sweep_negate{};
  bool ch1_sweep_enabled{};
  bool ch1_sweep_negate_used{};

  // LFSR
  std::uint16_t ch4_lfsr{0x7FFF};

  // Smoothed mixer controls (to reduce DC-offset step pops)
  float master_left_cur_{1.0f}, master_left_target_{1.0f}, master_left_step_{0.0f};
  float master_right_cur_{1.0f}, master_right_target_{1.0f}, master_right_step_{0.0f};
  std::array<float, 4> route_l_cur_{}, route_l_target_{}, route_l_step_{};
  std::array<float, 4> route_r_cur_{}, route_r_target_{}, route_r_step_{};

  // Highpass filter
  float dc_x1_l{}, dc_y1_l{};
  float dc_x1_r{}, dc_y1_r{};
};

#endif // GBC_APU_HPP
