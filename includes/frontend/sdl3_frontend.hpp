#ifndef __RENDERER_H
#define __RENDERER_H

#include "frontend/frontend.hpp"
#include <ImGuiFileDialog.h>
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>

class SDL3Frontend final : public Frontend {
public:
  SDL3Frontend();
  ~SDL3Frontend();

  static constexpr int framebuf_width = 160;
  static constexpr int framebuf_height = 144;
  static constexpr int scale = 4;

  std::array<std::uint32_t, framebuf_height * framebuf_width> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c = 0x00FFFFFF) override;
  void queue_audio_samples(const float *samples,
                         std::size_t sample_count) override;
  void start() override;

  bool consume_load_request(std::string &rom_path);
  void set_status_message(std::string message);
  void poll_events();
  void present_ui();

private:
  void emulation_thread_fn(std::stop_token st, cart c);
  void join_emu_thread_if_running();
  std::jthread emulation_thread{};

  // SDL3 display boilerplate
  SDL_Renderer *renderer{};
  SDL_Texture *texture{};
  SDL_Window *window{};
  SDL_AudioDeviceID audio_device{};
  SDL_AudioSpec audio_spec{};
  SDL_AudioStream *audio_stream{};

  struct UiState {
    bool show_load_window{true};
    bool show_settings_window{false};
    bool request_load{false};
    bool fast_forward{false};
    bool force_mono_dmg{false};
    std::string rom_path{};
    std::string status_message{};
    std::optional<std::size_t> waiting_for_bind{};
    int  keybind_preset_index{};          // default preset selected
  };

  struct EmulatorState {
    std::atomic<bool> fast_forward{};
    std::atomic<bool> is_cgb{};
  };

  struct InputState {
    std::atomic<byte_t> buttons{};
  };

  // enum KeyIndex : int { KRight, KLeft, KUp, KDown, KA, KB, KSelect, KStart, KCount };
  static constexpr std::array<Joypad::JoypadButton, 8> button_order{
    Joypad::JoypadButton::RIGHT,  Joypad::JoypadButton::LEFT,
    Joypad::JoypadButton::UP,     Joypad::JoypadButton::DOWN,
    Joypad::JoypadButton::A,      Joypad::JoypadButton::B,
    Joypad::JoypadButton::SELECT, Joypad::JoypadButton::START};
  static constexpr int KCount = 8;
  std::array<SDL_Keycode, KCount> keybinds{};
  struct KeybindPreset {
    const char* name;
    std::array<SDL_Keycode, KCount> keys;
  };

  static constexpr std::array<KeybindPreset, 4> kPresets{{
    { "WASD",
    { SDLK_D, SDLK_A, SDLK_W, SDLK_S, SDLK_J, SDLK_K, SDLK_BACKSPACE, SDLK_RETURN } },

    { "Arrows",
      { SDLK_RIGHT, SDLK_LEFT, SDLK_UP, SDLK_DOWN, SDLK_Z, SDLK_X, SDLK_RSHIFT, SDLK_RETURN } },

    { "IJKL",
      { SDLK_L, SDLK_J, SDLK_I, SDLK_K, SDLK_Z, SDLK_X, SDLK_BACKSPACE, SDLK_RETURN } },

    // Just a placeholder for custom bindings
    { "Custom", { SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN,
                  SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN } },
  }};

  static constexpr int kCustomPresetIndex = static_cast<int>(kPresets.size()) - 1;

  // Helper: apply preset -> keybinds
  static void ApplyPreset(std::array<SDL_Keycode, KCount>& keybinds, int preset_index) {
    if (preset_index < 0 || preset_index >= static_cast<int>(kPresets.size())) return;
    if (preset_index == kCustomPresetIndex) return; // don't clobber custom
    keybinds = kPresets[preset_index].keys;
  }

  const std::uint32_t format_pixel_data(std::uint32_t px) const;
  const std::uint32_t *front_buffer() const;
  void update_button_state(SDL_Keycode key, bool pressed);
  byte_t button_mask_for_key(SDL_Keycode key) const;
  const int calc_sync_cycles() const;
  void build_ui();

  // Frame buffer and rendering control
  std::array<std::unique_ptr<std::uint32_t[]>, 2> framebuffers;
  std::uint32_t pixels_rendered{};
  std::atomic<int> front_index{0};
  mutable std::mutex ui_mutex{};

  // System keep-alive
  std::atomic<bool> running{};
  EmulatorState emu_state{};
  UiState ui_state{};
  InputState input_state{};

  IGFD::FileDialogConfig config;
  ImVec2 max_size, min_size;
};

#endif // __RENDERER_H
