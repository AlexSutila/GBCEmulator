#ifndef GBC_SDL3_GUI_HPP
#define GBC_SDL3_GUI_HPP
#define IMGUI_DEFINE_MATH_OPERATORS

#include "common.hpp"
#include "sdl_host.hpp"
#include <functional>
#include <imgui.h>
#include <ImGuiFileDialog.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <backends/imgui_impl_sdl3.h>

class GbcImGui {
  static constexpr std::string_view rom_filters =
    "ROM files (*.gb *.gbc){.gb,.gbc},ZIP files (*.zip){.zip},All files (*.*){.*}";
  static constexpr std::string_view bios_filters =
      "BIOS files (*.bin){.bin},All files (*.*){.*}";
  static constexpr std::array<std::string_view, KCount> control_labels{
    "Right", "Left", "Up", "Down", "A", "B", "Select", "Start"};
  static constexpr std::array<std::string_view, GK_COUNT> general_labels{
    "FF Toggle", "FF (Hold)", "Vol Up", "Vol Down", "Monochrome",
    "Quicksave", "Quickload"};
  static constexpr float max_font_scale = 3.0f;
  static constexpr float base_font_size = 16.0f;
  const std::string font = "../fonts/TerminessNerdFontMono-Regular.ttf";
  struct ThirdPartyProject {
    std::string_view name;
    std::string_view url;
    std::string_view license;
  };

  static constexpr std::array<ThirdPartyProject, 11> kThirdPartyProjects{{
    {"curl",            "https://curl.se",                                        "MIT-like license"},
    {"emscripten",      "https://emscripten.org",                                 "MIT/Expat license"},
    {"Dear ImGUI",      "https://github.com/ocornut/imgui",                       "MIT license"},
    {"ImGUIFileDialog", "https://github.com/aiekick/ImGuiFileDialog",             "MIT license"},
    {"json",            "https://github.com/nlohmann/json",                       "MIT license"},
    {"mbedTLS",         "https://www.trustedfirmware.org/projects/mbed-tls",      "Apache 2.0 OR GPL 2.0 or later license"},
    {"miniz",           "https://github.com/richgel999/miniz",                    "MIT license"},
    {"PicoSHA2",        "https://github.com/okdshin/PicoSHA2",                    "MIT license"},
    {"pybind11",        "https://github.com/pybind/pybind11",                     "BSD-like license"},
    {"raylib",          "https://www.raylib.com",                                 "zlib license"},
    {"SDL3",            "https://www.libsdl.org",                                 "zlib license"},
  }};

public:
  struct SavestateManagerCallbacks {
    std::function<void(const std::string &label)> queue_manual_save;
    std::function<void()> request_load_most_recent;
    std::function<void()> refresh;
    std::function<void(const std::filesystem::path &path)> queue_load;
    std::function<void(const std::filesystem::path &path)> delete_state;
  };

  void init(const SDLHost& host);
  void shutdown();

  // The main render pass for UI
  static void new_frame() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
  }
  void use_main_context();
  void use_tool_context();
  [[nodiscard]] bool has_tool_window() const { return tool_context_.context != nullptr; }
  [[nodiscard]] SDL_Renderer *get_tool_renderer() const { return tool_context_.renderer; }
  [[nodiscard]] bool sync_tool_window(UiState &state);
  void render(UiState& state, SDLHost& host);
  void render_tool_windows(UiState &state, SDLHost &host);
  void render_tool_window_frame() const;
  static void end_frame() {ImGui::Render();}

  void update_rom_path(const std::string& rom_path);
  void update_bios_path(const std::string& bios_path);
  void clear_bios_path();
  bool process_event(const SDL_Event& e, UiState& ui_state);
  static void build_savestate_manager_window(
      UiState &state, SDL_Renderer *renderer, bool emulator_ready,
      const std::filesystem::path &savestate_dir,
      std::array<char, 96> &manual_label_input,
      std::vector<SavestateEntry> &savestate_entries,
      std::optional<std::filesystem::path> &savestate_selected_path,
      const SavestateManagerCallbacks &callbacks);

  static void push_notification(UiState& state, LogLevel level, const std::string& type, const std::string& summary,
                                         const std::string& details = "", time_t timestamp= std::time(nullptr));

  // Accessors
  [[nodiscard]] const Settings& get_settings_c() const { return settings; }
  [[nodiscard]] Settings& get_settings() { return settings; }

private:
  struct ImGuiContextState {
    ImGuiContext *context{nullptr};
    SDL_Window *window{nullptr};
    SDL_Renderer *renderer{nullptr};
    float dpi_scale{1.0f};
    bool owns_window{false};
  };

  Settings settings;
  float dpi_scale{1.0f};
  ImGuiContextState main_context_{};
  ImGuiContextState tool_context_{};
  bool tool_window_visible_{false};

  void init_context(ImGuiContextState &ctx, SDL_Window *window,
                    SDL_Renderer *renderer, bool owns_window);
  void shutdown_context(ImGuiContextState &ctx);
  void activate_context(ImGuiContextState &ctx);
  void update_dpi_scale(ImGuiContextState &ctx, float new_scale);
  static void close_tool_windows(UiState &state);
  [[nodiscard]] bool wants_detached_tool_window(const UiState &state) const;

  void build_main_menu_bar(UiState& state) const;
  void build_status_bar(UiState &state) const;
  void build_file_dialogs(UiState& state) const;
  void build_settings_window(UiState& state, SDLHost& host);
  void build_cheats_window(UiState& state);
  void build_keybinds_window(UiState& state);
  static void build_about_window(UiState& state);
  static void build_cart_info_window(UiState& state);
  void build_notification_window(UiState& state) const;
  void build_rom_source_window(UiState& state) const;

  // Helpers
  IGFD::FileDialogConfig rom_sel_conf;
  IGFD::FileDialogConfig bios_sel_conf;
  [[nodiscard]] std::tuple<ImVec2, ImVec2> get_min_dialog_size() const ;
  static ImVec4 get_darkened_color(ImVec4 color, float factor);
  static void apply_keybind_preset(std::array<SDL_Keycode, 8>& array, int keybind_preset_index);
  static ImVec4 get_level_color(LogLevel level) ;
  static void push_transient_status(UiState& state, LogLevel level, const std::string& type,
                                    const std::string& summary, const std::string& details = "",
                                    Uint64 duration_ms = 2000);
  static void populate_credits();
};

#endif //GBC_SDL3_GUI_HPP
