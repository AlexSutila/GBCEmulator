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
  std::string rom_dir{};
  std::array<SDL_Keycode, 8> keybinds = {
    SDLK_D, SDLK_A, SDLK_W, SDLK_S, SDLK_J, SDLK_K, SDLK_BACKSPACE, SDLK_RETURN
};
  static Settings load(const std::string& filename = "config.json");
  void save(const std::string& filename = "config.json") const;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Settings,
    volume,
    force_mono_dmg,
    keybind_preset_index,
    rom_dir,
    keybinds
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
#endif