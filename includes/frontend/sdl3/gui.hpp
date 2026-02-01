#ifndef GBC_GUI_HPP
#define GBC_GUI_HPP

#pragma once
#include "common.hpp"
#include "sdl_host.hpp"
#include "imgui.h"
#include "ImGuiFileDialog.h"
#include <backends/imgui_impl_sdlrenderer3.h>
#include <backends/imgui_impl_sdl3.h>

class GbcImGui {
  static constexpr std::string_view rom_filters =
    "ROM files (*.gb *.gbc){.gb,.gbc},All files (*.*){.*}";
  static constexpr std::string_view bios_filters =
      "BIOS files (*.bin){.bin},All files (*.*){.*}";
  static constexpr std::array<std::string_view, KCount> control_labels{
    "Right", "Left", "Up", "Down", "A", "B", "Select", "Start"};
  static constexpr std::array<std::string_view, 5> general_labels{
    "FF Toggle", "FF (Hold)", "Vol Up" , "Vol Down", "Monochrome"};
  static constexpr float max_font_scale = 3.0f;
  static constexpr float base_font_size = 16.0f;
  const std::string font = "../fonts/TerminessNerdFontMono-Regular.ttf";

public:
  void init(const SDLHost& host);
  void shutdown() const;

  // The main render pass for UI
  static void new_frame() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
  }
  void render(UiState& state, SDLHost& host);
  static void end_frame() {ImGui::Render();}

  void update_rom_path(const std::string& rom_path);
  void update_bios_path(const std::string& bios_path);
  bool process_event(const SDL_Event& e, UiState& ui_state);

  static void push_notification(UiState& state, LogLevel level, const std::string& type, const std::string& summary,
                                        const std::string& details = "", time_t timestamp= std::time(nullptr));

  // Accessors
  [[nodiscard]] const Settings& get_settings_c() const { return settings; }
  [[nodiscard]] Settings& get_settings() { return settings; }

private:
  Settings settings;
  float dpi_scale{1.0f};

  void build_main_menu_bar(UiState& state) const;
  static void build_status_bar(UiState &state);
  static void build_file_dialogs(UiState& state);
  void build_settings_window(UiState& state, SDLHost& host);
  void build_keybinds_window(UiState& state);
  static void build_notification_window(UiState& state);

  // Helpers
  IGFD::FileDialogConfig rom_sel_conf;
  IGFD::FileDialogConfig bios_sel_conf;
  void update_dpi_scale(float new_scale);
  static std::tuple<ImVec2, ImVec2> get_min_dialog_size() ;
  static ImVec4 get_darkened_color(ImVec4 color, float factor);
  static void apply_keybind_preset(std::array<SDL_Keycode, 8>& array, int keybind_preset_index);
  static ImVec4 get_level_color(LogLevel level) ;
};

#endif //GBC_GUI_HPP