#ifndef GBC_COMMON_HPP
#define GBC_COMMON_HPP

#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "emu_types.hpp"
#include "frontend/logger.hpp"

/* ---------- Settings ---------- */
/**
 * For future reference: to add a new setting
 * 1. Add it here with its default value
 * 2. Update the macro below
 */
struct Settings {
  float volume{0.5f};
  bool force_mono_dmg{false};
  int keybind_preset_index{};
  std::string rom_dir{"."};
  std::string prev_bios_path;
  std::string bios_dir{"."};
  std::array<SDL_Keycode, 8> keybinds{SDLK_D,         SDLK_A,     SDLK_W,
                                      SDLK_S,         SDLK_J,     SDLK_K,
                                      SDLK_BACKSPACE, SDLK_RETURN};
  std::array<SDL_Keycode, 5> general_keybinds{SDLK_G, SDLK_F, SDLK_EQUALS,
                                              SDLK_MINUS, SDLK_M};
  std::vector<std::string> recent_roms;
  static Settings load(const std::string &filename = ".gbc.config.json");
  void save(const std::string &filename = ".gbc.config.json") const;
  void add_recent_rom(const std::string &path);
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, volume,
                                                force_mono_dmg,
                                                keybind_preset_index, rom_dir,
                                                prev_bios_path, bios_dir,
                                                keybinds, recent_roms)

inline Settings Settings::load(const std::string &filename) {
  Settings s;
  if (std::ifstream file(filename); file.is_open()) {
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
  if (std::ofstream file(filename); file.is_open()) {
    const nlohmann::json j = *this;
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

/* ---------- Input ---------- */
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
    {"Custom", // Just a placeholder for custom bindings
     {SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN,
      SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN}},
}};
static constexpr int kCustomPresetIndex = static_cast<int>(kPresets.size()) - 1;

struct InputState {
  std::atomic<byte_t> buttons{};
};

/* ---------- Notifications ---------- */
struct Notification {
  int id;
  LogLevel level;
  std::string type;    // e.g., "BIOS", "Audio"
  std::string summary; // e.g., "File not found"
  std::string details; // Full path, stack trace, etc.
  std::time_t timestamp;
};

/* ---------- UI State ---------- */
struct UiState {
  bool show_settings{false};
  bool show_main_debug_viewer{false};
  bool show_breakpoints{false};
  bool show_ppu_viewer{false};
  bool show_keybinds{false};
  bool show_about{false};
  bool fast_forward{false};

  // Hex memory reader specific
  bool show_memory_viewer{false};
  addr_t hex_view_base_addr{0};

  // File requests
  bool request_load_rom{false};
  std::string load_rom_path;
  bool request_load_bios{false};
  std::string load_bios_path;
  bool request_quit{false};

  // Audio Device Cache
  std::vector<std::string> audio_device_names;
  std::vector<SDL_AudioDeviceID> audio_device_ids;
  int current_audio_dev_idx{};

  // Notification (errors)
  std::vector<Notification> notifications;
  bool show_notifications{false};
  int next_notify_id{};

  // FPS Tracking
  double current_fps{};
  float frame_time_ms{};

  // Miscellaneous
  std::optional<std::size_t> waiting_for_bind{};
  std::string cart_info{"No ROM loaded"};
};

#endif // GBC_COMMON_HPP
