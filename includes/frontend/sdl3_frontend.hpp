#ifndef __RENDERER_H
#define __RENDERER_H

#include "debugger/breakpoint.hpp"
#include "frontend/frontend.hpp"
#include "imgui.h"
#include <ImGuiFileDialog.h>
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stop_token>
#include <string>
#include <tuple>

/**
 * For future reference: to add a new setting
 * 1. Add it here
 * 2. Update the marcro below
 */
struct Settings {
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

struct UiState {
  std::vector<SDL_AudioDeviceID> output_device_ids;
  std::vector<std::string> output_device_names;
  std::optional<std::string> bios_path{std::nullopt};
  std::optional<std::size_t> waiting_for_bind{};
  std::string status_message{};
  std::string rom_path{};
  int output_device_index = 0;
  bool show_load_window{false};
  bool show_settings_window{false};
  bool show_breakpoints_window{false};
  bool show_debug_window{false};
  bool request_load_bios{false};
  bool request_load_rom{false};
  bool fast_forward{false};
};

struct DebuggerState {
  Debug::BreakReason reason{Debug::BRK_CONTINUE};
  std::string sys_state{};
  std::string cpu_state{};
  std::string ie_state{};
  std::string if_state{};
  std::string disasm{};
  bool stopped{false};
};

struct BreakpointPrompt {
  addr_t addr{0};
  bool read{false};
  bool write{false};
  bool execute{false};
  bool show{false};
};

struct EmulatorState {
  std::atomic<bool> fast_forward{};
  std::atomic<bool> is_cgb{};
};

struct InputState {
  std::atomic<byte_t> buttons{};
};

static constexpr int KCount = 8;
struct KeybindPreset {
  const char *name;
  std::array<SDL_Keycode, KCount> keys;
};

static constexpr std::array<Joypad::JoypadButton, KCount> button_order{
    Joypad::JoypadButton::RIGHT,  Joypad::JoypadButton::LEFT,
    Joypad::JoypadButton::UP,     Joypad::JoypadButton::DOWN,
    Joypad::JoypadButton::A,      Joypad::JoypadButton::B,
    Joypad::JoypadButton::SELECT, Joypad::JoypadButton::START};

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
    {"Custom", // Just a placeholder for custom bindings
     {SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN,
      SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN}},
}};
static constexpr int kCustomPresetIndex = static_cast<int>(kPresets.size()) - 1;

class SDL3Frontend final : public Frontend {
  static constexpr int framebuf_height = 144;
  static constexpr int framebuf_width = 160;
  static constexpr int framebuf_size = framebuf_width * framebuf_height;
  static constexpr int scale = 4;

public:
  SDL3Frontend();
  ~SDL3Frontend();

  std::array<std::uint32_t, framebuf_size> get_frame() override;
  void put_pixel(int x, int y, std::uint32_t c) override;
  void queue_audio_samples(const float *samples,
                           std::size_t sample_count) override;
  void clear(std::uint32_t c = 0x00FFFFFF) override;
  void start() override;

private:
  std::tuple<ImVec2, ImVec2> calc_winsize_bounds() const;

  // SDL3 display boilerplate
  SDL_Renderer *renderer{};
  SDL_Texture *texture{};
  SDL_Window *window{};
  SDL_AudioDeviceID audio_device{};
  SDL_AudioSpec audio_spec{};
  SDL_AudioStream *audio_stream{};

  // General UI helpers
  using opt_string_t = std::optional<std::string>;
  void build_main_menu_bar(ImVec2, ImVec2);
  void build_rom_selection_dialog(ImVec2, ImVec2);
  void build_bios_selection_dialog(ImVec2, ImVec2);
  void build_settings_dialog();
  bool consume_load_bios_request(opt_string_t &bios_path);
  bool consume_load_rom_request(std::string &rom_path);
  void set_status_message(std::string message);
  void present_ui(); // Invoke to render UI to screen
  void build_ui();   // Wrapper around all UI construction
  IGFD::FileDialogConfig bios_sel_conf;
  IGFD::FileDialogConfig rom_sel_conf;
  Settings settings{};
  UiState ui_state{};
  mutable std::mutex ui_mutex{};

  // Debugger UI helpers
  void build_config_breakpoint_dialog();
  void build_breakpoint_dialog();
  void read_system_dbg_state(); // Snags debug info from emulator
  void build_debug_dialog();
  std::condition_variable dbg_cv{};
  BreakpointPrompt bp_prompt{};
  DebuggerState dbg_state{};
  mutable std::mutex dbg_mutex{};

  // Frame buffer and rendering control
  const std::uint32_t format_pixel_data(std::uint32_t px) const;
  const std::uint32_t *get_front_buffer() const;
  std::array<std::unique_ptr<std::uint32_t[]>, 2> framebuffers;
  std::atomic<int> front_index{0};

  // Input / joypad update helpers
  void apply_keybind_preset(std::array<SDL_Keycode, KCount> &keybinds,
                            int preset_index);
  void update_button_state(SDL_Keycode key, bool pressed);
  byte_t button_mask_for_key(SDL_Keycode key) const;
  void poll_events();
  InputState input_state{};

  // Audio helpers
  bool switch_output_device_by_index(int idx);
  void refresh_output_devices();
  mutable std::mutex audio_mutex;

  // Emulation runs in a thread separated from UI and Debug tools
  void emulation_thread_fn(std::stop_token st, cart c, opt_string_t bios);
  void join_emu_thread_if_running();
  std::jthread emulation_thread{};
  std::atomic<bool> running{};
  EmulatorState emu_state{};
};

#endif // __RENDERER_H
