#ifndef __RENDERER_H
#define __RENDERER_H

#include "frontend/frontend.hpp"
#include "imgui.h"
#include <ImGuiFileDialog.h>
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stop_token>
#include <string>
#include <tuple>

struct Settings {
  // For future reference: to add a new setting
  // 1. Add it here
  // 2. Update the marcro below
  float volume = 0.5f;
  bool force_mono_dmg = false;
  int keybind_preset_index = 0;
  std::string rom_dir = ".";
  std::array<SDL_Keycode, 8> keybinds = {SDLK_D,         SDLK_A,     SDLK_W,
                                         SDLK_S,         SDLK_J,     SDLK_K,
                                         SDLK_BACKSPACE, SDLK_RETURN};
  std::vector<std::string> recent_roms;
  static Settings load(const std::string &filename = ".gbc.config.json");
  void save(const std::string &filename = ".gbc.config.json") const;
  void add_recent_rom(const std::string &path);
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Settings, volume, force_mono_dmg,
                                   keybind_preset_index, rom_dir, keybinds,
                                   recent_roms)

inline Settings Settings::load(const std::string &filename) {
  Settings s;
  std::ifstream file(filename);
  if (file.is_open()) {
    try {
      nlohmann::json j;
      file >> j;
      s = j.get<Settings>();
    } catch (...) { /* Fallback to defaults on corrupt file */
    }
  }
  return s;
}

inline void Settings::save(const std::string &filename) const {
  std::ofstream file(filename);
  if (file.is_open()) {
    nlohmann::json j = *this;
    file << j.dump(4); // Indented 4 spaces
  }
}

inline void Settings::add_recent_rom(const std::string &path) {
  // Remove if already exists (so we can move it to top)
  const auto it = std::ranges::remove(recent_roms, path).begin();
  recent_roms.erase(it, recent_roms.end());
  recent_roms.insert(recent_roms.begin(), path);
  // Keep only the last 10 entries
  if (recent_roms.size() > 10) {
    recent_roms.resize(10);
  }
}

class SDL3Frontend final : public Frontend {
public:
  SDL3Frontend();
  ~SDL3Frontend();
  static constexpr int framebuf_height = 144;
  static constexpr int framebuf_width = 160;
  static constexpr int scale = 4;

  std::array<std::uint32_t, framebuf_height * framebuf_width>
  get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c = 0x00FFFFFF) override;
  void queue_audio_samples(const float *samples,
                           std::size_t sample_count) override;
  void refresh_output_devices();
  bool switch_output_device_by_index(int idx);
  void start() override;

  bool consume_load_bios_request(std::optional<std::string> &bios_path);
  bool consume_load_rom_request(std::string &rom_path);
  void set_status_message(std::string message);
  void poll_events();
  void present_ui();

private:
  void emulation_thread_fn(std::stop_token st, cart c,
                           std::optional<std::string> bios);
  void join_emu_thread_if_running();
  std::jthread emulation_thread{};

  std::tuple<ImVec2, ImVec2> get_sizing_metadata() const;
  void build_main_menu_bar(ImVec2, ImVec2);
  void build_rom_selection_dialog(ImVec2, ImVec2);
  void build_bios_selection_dialog(ImVec2, ImVec2);
  void build_settings_dialog(ImVec2, ImVec2);
  void build_ui();

  // SDL3 display boilerplate
  SDL_Renderer *renderer{};
  SDL_Texture *texture{};
  SDL_Window *window{};
  SDL_AudioDeviceID audio_device{};
  SDL_AudioSpec audio_spec{};
  SDL_AudioStream *audio_stream{};

  Settings settings_;
  struct UiState {
    bool show_load_window{true};
    bool show_settings_window{false};
    bool request_load_bios{false};
    bool request_load_rom{false};
    bool fast_forward{false};
    std::optional<std::string> bios_path{std::nullopt};
    std::string rom_path{};
    std::string status_message{};
    // Keybinding
    std::optional<std::size_t> waiting_for_bind{};
    // Sound / volume control

    int output_device_index = 0; // 0 = system default
                                 // 1..N = physical device ids
    std::vector<SDL_AudioDeviceID> output_device_ids;
    std::vector<std::string> output_device_names;
  };

  struct EmulatorState {
    std::atomic<bool> fast_forward{};
    std::atomic<bool> is_cgb{};
  };

  struct InputState {
    std::atomic<byte_t> buttons{};
  };

  static constexpr std::array<Joypad::JoypadButton, 8> button_order{
      Joypad::JoypadButton::RIGHT,  Joypad::JoypadButton::LEFT,
      Joypad::JoypadButton::UP,     Joypad::JoypadButton::DOWN,
      Joypad::JoypadButton::A,      Joypad::JoypadButton::B,
      Joypad::JoypadButton::SELECT, Joypad::JoypadButton::START};
  static constexpr int KCount = 8;
  struct KeybindPreset {
    const char *name;
    std::array<SDL_Keycode, KCount> keys;
  };

  static constexpr std::array<KeybindPreset, 4> kPresets{{
      {"WASD",
       {SDLK_D, SDLK_A, SDLK_W, SDLK_S, SDLK_J, SDLK_K, SDLK_BACKSPACE,
        SDLK_RETURN}},

      {"Arrows",
       {SDLK_RIGHT, SDLK_LEFT, SDLK_UP, SDLK_DOWN, SDLK_Z, SDLK_X, SDLK_RSHIFT,
        SDLK_RETURN}},

      {"IJKL",
       {SDLK_L, SDLK_J, SDLK_I, SDLK_K, SDLK_Z, SDLK_X, SDLK_BACKSPACE,
        SDLK_RETURN}},

      // Just a placeholder for custom bindings
      {"Custom",
       {SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN,
        SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN}},
  }};

  mutable std::mutex audio_mutex;

  static constexpr int kCustomPresetIndex =
      static_cast<int>(kPresets.size()) - 1;

  // Helper: apply preset -> keybinds
  static void ApplyPreset(std::array<SDL_Keycode, KCount> &keybinds,
                          int preset_index) {
    if (preset_index < 0 || preset_index >= static_cast<int>(kPresets.size()))
      return;
    if (preset_index == kCustomPresetIndex)
      return; // don't clobber custom
    keybinds = kPresets[preset_index].keys;
  }

  const std::uint32_t format_pixel_data(std::uint32_t px) const;
  const std::uint32_t *front_buffer() const;
  void update_button_state(SDL_Keycode key, bool pressed);
  byte_t button_mask_for_key(SDL_Keycode key) const;

  // Frame buffer and rendering control
  std::array<std::unique_ptr<std::uint32_t[]>, 2> framebuffers;
  std::atomic<int> front_index{0};
  mutable std::mutex ui_mutex{};

  // System keep-alive
  std::atomic<bool> running{};
  EmulatorState emu_state{};
  UiState ui_state{};
  InputState input_state{};

  IGFD::FileDialogConfig bios_sel_conf;
  IGFD::FileDialogConfig rom_sel_conf;
  ImVec2 max_size, min_size;
};

#endif // __RENDERER_H
