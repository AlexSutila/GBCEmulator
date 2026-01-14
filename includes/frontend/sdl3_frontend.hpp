#ifndef __RENDERER_H
#define __RENDERER_H

#include "frontend/frontend.hpp"
#include <ImGuiFileDialog.h>
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <cstdint>
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

  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c = 0x00FFFFFF) override;
  void start() override;

  bool consume_load_request(std::string &rom_path);
  void set_status_message(std::string message);
  void poll_events();
  void present_ui();

private:
  void emulation_thread_fn(std::stop_token st, cart c);
  void join_emu_thread_if_running();
  void init_audio();
  void shutdown_audio();
  static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len);
  std::jthread emulation_thread{};

  // SDL3 display boilerplate
  SDL_Renderer *renderer{};
  SDL_Texture *texture{};
  SDL_Window *window{};

  struct UiState {
    bool show_load_window{true};
    bool show_settings_window{false};
    bool request_load{false};
    bool fast_forward{false};
    bool force_mono_dmg{false};
    std::string rom_path{};
    std::string status_message{};
    std::optional<std::size_t> waiting_for_bind{};
  };

  struct EmulatorState {
    std::atomic<bool> fast_forward{};
    std::atomic<bool> is_cgb{};
  };

  struct InputState {
    std::atomic<byte_t> buttons{};
  };

  struct AudioState {
    SDL_AudioDeviceID device{};
    SDL_AudioSpec spec{};
    std::atomic<std::uint32_t> sync_cycles{};
    std::atomic<bool> active{};
    double phase{};
    double cycle_remainder{};
  };

  /* Default keybind configuration */
  static constexpr std::array<Joypad::JoypadButton, 8> button_order{
      Joypad::JoypadButton::RIGHT,  Joypad::JoypadButton::LEFT,
      Joypad::JoypadButton::UP,     Joypad::JoypadButton::DOWN,
      Joypad::JoypadButton::A,      Joypad::JoypadButton::B,
      Joypad::JoypadButton::SELECT, Joypad::JoypadButton::START};
  static constexpr std::array<SDL_Keycode, 8> default_keybinds{
      SDLK_D, SDLK_A, SDLK_W,         SDLK_S,
      SDLK_J, SDLK_K, SDLK_BACKSPACE, SDLK_ESCAPE};
  std::array<SDL_Keycode, 8> keybinds{default_keybinds};

  const std::uint32_t format_pixel_data(std::uint32_t px) const;
  const std::uint32_t *front_buffer() const;
  void build_ui();
  void update_button_state(SDL_Keycode key, bool pressed);
  byte_t button_mask_for_key(SDL_Keycode key) const;

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
  AudioState audio_state{};

  IGFD::FileDialogConfig config;
  ImVec2 max_size, min_size;
};

#endif // __RENDERER_H
