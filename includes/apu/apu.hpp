#ifndef __APU_H
#define __APU_H

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class AddressBus;
class Frontend;

class APU {
public:
  APU(AddressBus &bus, Frontend &frontend);
  void step();

private:
  class AudioRegister final : public MMIORegister {
  public:
    using WriteCallback = std::function<void(byte_t)>;
    using ReadCallback = std::function<byte_t(byte_t)>;

    void configure(byte_t initial, WriteCallback on_write,
                   ReadCallback on_read = {});
    void write(byte_t value) override;
    byte_t read() override;

  private:
    byte_t state{};
    WriteCallback on_write{};
    ReadCallback on_read{};
  };

  void register_mmio();
  void trigger_channel1();
  void generate_sample();
  float channel1_sample() const;

  static constexpr int sample_rate_hz = 48000;
  static constexpr int frames_per_buffer = 512;
  static constexpr double cpu_clock_hz = 4'194'304.0;

  AddressBus &bus_;
  Frontend &frontend_;
  std::array<AudioRegister, 0x17> audio_registers{};
  std::array<MMIORegister, 0x10> wave_ram{};

  std::vector<float> mix_buffer{};
  std::size_t frame_cursor{};
  double cycle_accumulator{};

  byte_t nr10{};
  byte_t nr11{};
  byte_t nr12{};
  byte_t nr13{};
  byte_t nr14{};
  byte_t nr50{};
  byte_t nr51{};
  byte_t nr52{};

  bool channel1_enabled{};
  double channel1_phase{};
};

#endif // __APU_H
