#ifndef GBC_FRONTEND_HPP
#define GBC_FRONTEND_HPP

#pragma once
#include "frontend/frontend.hpp"
#include "common.hpp"
#include "sdl_host.hpp"
#include "gui.hpp"
#include "debugger.hpp"
#include <thread>
#include <atomic>

class SDL3Frontend final : public Frontend {
  static constexpr int framebuf_height{144};
  static constexpr int framebuf_width{160};
  static constexpr int framebuf_size{framebuf_width * framebuf_height};
  static constexpr int scale{4};
  static constexpr unsigned black{0xFF000000};
  static constexpr std::array<Joypad::JoypadButton, 8> button_order{
    Joypad::JoypadButton::RIGHT,  Joypad::JoypadButton::LEFT,
    Joypad::JoypadButton::UP,     Joypad::JoypadButton::DOWN,
    Joypad::JoypadButton::A,      Joypad::JoypadButton::B,
    Joypad::JoypadButton::SELECT, Joypad::JoypadButton::START};
  static constexpr int max_catchup_cycles{70'224 / 4}; // 1/4 second worth of cycles at 4.19MHz
  static constexpr int target_queue_ms{20};

public:
  SDL3Frontend();
  ~SDL3Frontend() override;

  // Frontend Interface Overrides
  void start() override;
  std::array<std::uint32_t, 160 * 144> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void queue_audio_samples(const float *samples, size_t count) override;
  void clear(std::uint32_t c) override;

private:
  // Subsystems
  SDLHost host;
  GbcImGui gui;
  DebuggerImGui debugger;
  UiState ui_state;

  // Emulation state
  std::jthread emulation_thread;
  std::atomic<bool> running{true};
  std::atomic<bool> is_cgb{false};
  std::atomic<bool> fast_forward{true};

  // Video buffers
  std::array<std::unique_ptr<std::uint32_t[]>, 2> framebuffers;
  std::atomic<int> front_index{0};
  const std::uint32_t *get_front_buffer() const;

  // ROM loading
  bool consume_load_rom_request(std::string& rom_path);
  bool consume_load_bios_request(std::optional<std::string>& bios_path);

  // Main loop helpers
  void process_events();
  void render_frame();
  void emulation_thread_fn(const std::stop_token& st, const cart& c, const std::optional<std::string>& bios);
  void join_emu_thread_if_running();
  byte_t button_mask_for_key(SDL_Keycode key) const;

  // Input helpers
  InputState input_state{};
  void handle_keypress(SDL_Keycode key, bool pressed);

  mutable std::mutex ui_mutex;

  // Shouldn't be here
  void set_status_message(std::string message);
};

#endif //GBC_FRONTEND_HPP