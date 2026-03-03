#ifndef GBC_SDL3_COMMON_HPP
#define GBC_SDL3_COMMON_HPP

#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
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
  static constexpr int default_max_quicksaves = 10;
  float volume{0.5f};
  bool force_mono_dmg{false};
  int keybind_preset_index{};
  std::string rom_dir{"."};
  std::string prev_bios_path;
  std::string bios_dir{"."};
  std::string save_root_dir{"./saves"};
  std::string savestate_root_dir{"./savestates"};
  int max_quicksaves{default_max_quicksaves};
  std::array<SDL_Keycode, 8> keybinds{SDLK_D,         SDLK_A,     SDLK_W,
                                      SDLK_S,         SDLK_J,     SDLK_K,
                                      SDLK_BACKSPACE, SDLK_RETURN};
  std::array<SDL_Keycode, 7> general_keybinds{
      SDLK_G, SDLK_F, SDLK_EQUALS, SDLK_MINUS, SDLK_M, SDLK_F5, SDLK_F8};
  std::vector<std::string> recent_roms;
  struct CheatEntry {
    bool enabled{true};
    std::string name;
    std::string code;
    std::string notes;
    int format{}; // 0=Auto, 1=GameShark, 2=Game Genie, 3=Raw
  };
  std::vector<CheatEntry> cheats;
  static Settings load(const std::string &filename = ".gbc.config.json");
  void save(const std::string &filename = ".gbc.config.json") const;
  void add_recent_rom(const std::string &path);
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings::CheatEntry, enabled,
                                                name, code, notes, format)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, volume,
                                                force_mono_dmg,
                                                keybind_preset_index, rom_dir,
                                                prev_bios_path, bios_dir,
                                                save_root_dir,
                                                savestate_root_dir,
                                                max_quicksaves,
                                                keybinds, general_keybinds,
                                                recent_roms, cheats)

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
  if (s.max_quicksaves < 0)
    s.max_quicksaves = default_max_quicksaves;
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

struct SavestateEntry {
  std::filesystem::path state_path;
  std::filesystem::path thumb_path;
  std::string kind;
  std::string label;
  std::time_t created_at{};
  std::uintmax_t file_size{};
  int thumb_w{};
  int thumb_h{};
  SDL_Texture *thumb_texture{nullptr};
  bool thumb_texture_attempted{false};
};

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

enum GeneralKeybindIndex : std::size_t {
  GK_FF_TOGGLE = 0,
  GK_FF_HOLD,
  GK_VOL_UP,
  GK_VOL_DOWN,
  GK_MONOCHROME,
  GK_QUICKSAVE,
  GK_QUICKLOAD,
  GK_COUNT
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
  bool show_cheats{false};
  bool show_main_debug_viewer{false};
  bool show_breakpoints{false};
  bool show_ppu_viewer{false};
  bool show_keybinds{false};
  bool show_about{false};
  bool show_cart_info{false};
  bool show_savestate_manager{false};
  bool fast_forward{false};
  bool cheats_dirty{false};

  // Hex memory reader specific
  bool show_memory_viewer{false};
  addr_t hex_view_base_addr{0};

  // File requests
  bool request_load_rom{false};
  std::string load_rom_name;
  std::string load_rom_path;
  bool request_load_bios{false};
  std::string load_bios_path;
  bool request_unload_bios{false};
  bool request_quit{false};

  // ROM I/O status (downloads, unzip, etc.)
  bool io_busy{false};
  float io_progress{-1.0f}; // -1 = unknown/indeterminate
  std::string io_status;

  // Load-from-URL popup
  bool show_load_url_popup{false};
  char load_url_input[2048] = "";

  // ZIP chooser popup (when multiple ROMs exist in an archive)
  bool show_zip_picker_popup{false};
  std::string zip_picker_title;
  std::vector<std::string> zip_rom_entries;
  int zip_rom_selected_idx{0};
  int zip_picker_action{0}; // 0=none, 1=ok, 2=cancel

  // Audio Device Cache
  std::vector<std::string> audio_device_names;
  std::vector<SDL_AudioDeviceID> audio_device_ids;
  int current_audio_dev_idx{};

  // Notification (errors)
  std::vector<Notification> notifications;
  bool show_notifications{false};
  int next_notify_id{};
  std::string transient_status_text;
  std::string transient_status_details;
  LogLevel transient_status_level{LogLevel::Status};
  Uint64 transient_status_until_ticks{};

  // FPS Tracking
  double current_fps{};
  float frame_time_ms{};
  float menu_bar_height{0.0f};
  float status_bar_height{0.0f};

  // Miscellaneous
  std::optional<std::size_t> waiting_for_bind{};
  int selected_cheat_idx{-1};
  std::string cart_info{"No ROM loaded"};
};

#endif // GBC_SDL3_COMMON_HPP
