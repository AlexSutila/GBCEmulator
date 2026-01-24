#ifndef __SETTINGS_HPP
#define __SETTINGS_HPP

#include <string>
#include <fstream>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Settings {
  // For future reference: to add a new setting
  // 1. Add it here
  // 2. Update the marcro below
  float volume = 0.5f;
  bool force_mono_dmg = false;
  int keybind_preset_index = 0;
  std::string rom_dir = ".";
  std::array<SDL_Keycode, 8> keybinds = {
    SDLK_D, SDLK_A, SDLK_W, SDLK_S, SDLK_J, SDLK_K, SDLK_BACKSPACE, SDLK_RETURN
  };
  std::vector<std::string> recent_roms;
  static Settings load(const std::string& filename = "config.json");
  void save(const std::string& filename = "config.json") const;
  void add_recent_rom(const std::string& path);
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Settings,
    volume,
    force_mono_dmg,
    keybind_preset_index,
    rom_dir,
    keybinds,
    recent_roms
)

inline Settings Settings::load(const std::string& filename) {
  Settings s;
  std::ifstream file(filename);
  if (file.is_open()) {
    try {
      json j;
      file >> j;
      s = j.get<Settings>();
    } catch (...) { /* Fallback to defaults on corrupt file */ }
  }
  return s;
}


inline void Settings::save(const std::string& filename) const {
  std::ofstream file(filename);
  if (file.is_open()) {
    json j = *this;
    file << j.dump(4); // Indented 4 spaces
  }
}

inline void Settings::add_recent_rom(const std::string& path) {
  // Remove if already exists (so we can move it to top)
  const auto it = std::ranges::remove(recent_roms, path).begin();
  recent_roms.erase(it, recent_roms.end());
  recent_roms.insert(recent_roms.begin(), path);
  // Keep only the last 10 entries
  if (recent_roms.size() > 10) {
    recent_roms.resize(10);
  }
}
#endif