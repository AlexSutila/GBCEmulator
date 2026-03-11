#ifndef GBC_RAYLIB_FRONTEND_HPP
#define GBC_RAYLIB_FRONTEND_HPP

#include "frontend/frontend.hpp"
#include <array>
#include <filesystem>
#include <raylib.h>
#include <vector>

struct cart;

class RaylibFrontend final : public Frontend {
public:
  explicit RaylibFrontend(const cart &c);
  ~RaylibFrontend() override;

  std::array<std::uint32_t, 144 * 160> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c) override;
  void start() override;

  void read_inputs();
  void step_frame();
  void present();
  void pump_audio();
  void request_quicksave();
  void request_quickload();

#ifdef __EMSCRIPTEN__
  void tick_web();
  void flush_web_save_now();
#endif

  // samples: interleaved float PCM in [-1, 1]
  // sample_count: number of floats (NOT frames)
  void queue_audio_samples(const float *samples, std::size_t sample_count) override;

private:
  static constexpr auto fb_height = 144;
  static constexpr auto fb_width = 160;

  // Both invoked from the overridden read_inputs method
  static void read_controller_inputs(std::uint8_t &input_state);
  static void read_keyboard_inputs(std::uint8_t &input_state);
  void advance_cycles_with_preemption(std::size_t cycles);
  [[nodiscard]] bool has_pending_savestate_request() const;
  void process_pending_savestate_request();
  void process_quicksave_request();
  void process_quickload_request();
  [[nodiscard]] std::vector<std::uint8_t> capture_savestate_thumbnail_rgba() const;
  [[nodiscard]] static std::filesystem::path build_desktop_savestate_path(const cart &c);

  static constexpr int savestate_thumb_w = 80;
  static constexpr int savestate_thumb_h = 72;

  // We double buffer here, even though this is single threaded
  static constexpr auto nbuf = 2;
  std::size_t write_idx{0};
  std::size_t display_idx{0};
  bool frame_ready{false};

  // Double buffer, swap only when needed, prevents screen tears
  std::array<std::array<std::uint32_t, 144 * 160>, nbuf> frame_buf{};
  Texture2D texture{};

  // ---- Audio ----
  static constexpr unsigned audio_sample_rate = 48000; // sync with APU output rate
  static constexpr unsigned audio_channels = 2;        // assume interleaved stereo
#ifdef __EMSCRIPTEN__
  static constexpr unsigned audio_chunk_frames = 2048; // frames per UpdateAudioStream() / 21ms
#else
  static constexpr unsigned audio_chunk_frames = 512; // frames per UpdateAudioStream()
#endif

  AudioStream audio_stream{};
  bool audio_ready{false};
  int audio_prime{0}; // prime stream with N buffers at startup

// Soft cap queued audio to avoid accumulating large latency
// (rb_size counts floats, not frames)
#ifdef __EMSCRIPTEN__
  static constexpr std::size_t rb_soft_cap_frames = (audio_sample_rate * 120) / 1000; // ~120ms
#else
  static constexpr std::size_t rb_soft_cap_frames = (audio_sample_rate * 60) / 1000; // ~60ms
#endif
  static constexpr std::size_t rb_soft_cap_samples = rb_soft_cap_frames * audio_channels;

  // Time-based stepping accumulator (shared by native and web paths)
  double cycle_accum{0.0};
  void tick_common(double dt_ms);

#ifdef __EMSCRIPTEN__
  void restore_web_save();
  void poll_web_save_persistence();
  double web_last_ms{0.0};
  bool web_save_pending_flush{false};
  double web_save_flush_deadline_ms{0.0};
#endif

#ifdef __EMSCRIPTEN__
  static constexpr std::size_t ring_frames =
      audio_sample_rate * 2; // 2 seconds buffer (extra jitter tolerance)
#else
  static constexpr std::size_t ring_frames = audio_sample_rate; // 1 second buffer
#endif
  static constexpr std::size_t ring_samples = ring_frames * audio_channels;
  std::array<float, ring_samples> audio_rb{};
  std::size_t rb_head{0}, rb_tail{0}, rb_size{0}; // rb_size in floats

  std::array<float, audio_chunk_frames * audio_channels> audio_tmp{};
  bool quicksave_requested{false};
  bool quickload_requested{false};
#ifndef __EMSCRIPTEN__
  std::filesystem::path quick_savestate_path_{};
#endif
};

#endif // GBC_RAYLIB_FRONTEND_HPP
