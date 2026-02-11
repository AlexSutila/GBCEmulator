#include "frontend/sdl3/gui.hpp"

#include "memory/boot.hpp"
#include <ranges>
#include <sys/stat.h>

namespace fs = std::filesystem;

void GbcImGui::init(const SDLHost &host) {
  settings = Settings::load();

  /* ImGui initialization */
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  if (!ImGui_ImplSDL3_InitForSDLRenderer(host.get_window(),
                                         host.get_renderer()))
    throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
  if (!ImGui_ImplSDLRenderer3_Init(host.get_renderer()))
    throw std::runtime_error("Failed to initialize ImGui SDL renderer backend");

  const ImGuiIO &io = ImGui::GetIO();

  dpi_scale = SDL_GetWindowDisplayScale(host.get_window());
  update_dpi_scale(dpi_scale);

  // HiDPI scaling
  if (fs::exists(font)) {
    io.Fonts->AddFontFromFileTTF(font.c_str(), base_font_size);
  }

  rom_sel_conf.path = settings.rom_dir;
  rom_sel_conf.flags =
      ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ReadOnlyFileNameField;
  bios_sel_conf.path = settings.bios_dir;
  bios_sel_conf.flags = rom_sel_conf.flags =
      ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ReadOnlyFileNameField;

  if (settings.keybind_preset_index != kCustomPresetIndex) {
    apply_keybind_preset(settings.keybinds, settings.keybind_preset_index);
  }
}

void GbcImGui::shutdown() const {
  settings.save();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

void GbcImGui::render(UiState &state, SDLHost &host) {
  // Sync all logs (errors) generated since last cycle
  for (const auto new_logs = Logger::consume();
       const auto &[level, type, summary, message, timestamp] : new_logs) {
    push_notification(state, level, type, summary, message, timestamp);
  }
  build_main_menu_bar(state);
  build_status_bar(state);
  build_file_dialogs(state);
  if (state.show_settings)
    build_settings_window(state, host);
  if (state.show_keybinds)
    build_keybinds_window(state);
  if (state.show_notifications)
    build_notification_window(state);
  if (state.show_about)
    build_about_window(state);
}

// Returns true if the event was handled by the GUI and should be ignored by the
// game
bool GbcImGui::process_event(const SDL_Event &e, UiState &ui_state) {
  ImGui_ImplSDL3_ProcessEvent(&e);

  // Handle DPI changes
  if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
    // The event data contains the new scale, but it's safer to query the window
    // because SDL validates it against the specific display
    if (SDL_Window *window = SDL_GetWindowFromID(e.window.windowID)) {
      if (const float new_scale = SDL_GetWindowDisplayScale(window);
          std::abs(new_scale - dpi_scale) > 0.001f) {
        // We also want to resize the window to maintain physical size
        if (const Uint32 flags = SDL_GetWindowFlags(window);
            !(flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN))) {
          int w, h;
          SDL_GetWindowSize(window, &w, &h);

          // If moving 2.0x -> 1.0x, ratio is 0.5.
          // Window should shrink by half to look the same physical size.
          const float ratio = new_scale / dpi_scale;
          SDL_SetWindowSize(window,
                            static_cast<int>(static_cast<float>(w) * ratio),
                            static_cast<int>(static_cast<float>(h) * ratio));
        }
        update_dpi_scale(new_scale);
      }
    }
    return false; // Pass this event to the game (SDLHost might need it too)
  }

  if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
    // Handle key rebinding (highest priority - consumes input)
    if (ui_state.waiting_for_bind && e.type == SDL_EVENT_KEY_DOWN) {
      if (e.key.key != SDLK_ESCAPE) {
        settings.keybinds[*ui_state.waiting_for_bind] = e.key.key;
        settings.keybind_preset_index = kCustomPresetIndex;
      }
      ui_state.waiting_for_bind.reset();
      return true; // CONSUMED: this keypress won't press a button in-game
    }
    // Handle ImGui capture (e.g. typing in a file dialog)
    if (ImGui::GetIO().WantCaptureKeyboard) {
      return true; // CONSUMED
    }
  }
  return false; // NOT CONSUMED: pass to game loop
}

void GbcImGui::push_notification(UiState &state, const LogLevel level,
                                 const std::string &type,
                                 const std::string &summary,
                                 const std::string &details,
                                 const time_t timestamp) {
  Notification n;
  n.id = state.next_notify_id++;
  n.level = level;
  n.type = type;
  n.summary = summary;
  n.details = details.empty() ? summary : details;
  n.timestamp = timestamp;

  state.notifications.push_back(n);

  // Auto-open if it's a critical error
  // if (level == LogLevel::Error) state.show_notifications = true;
}

void GbcImGui::update_rom_path(const std::string &rom_path) {
  const auto new_rom_path = fs::path(rom_path).parent_path().string();
  rom_sel_conf.path = new_rom_path;
  settings.rom_dir = new_rom_path;
  settings.add_recent_rom(rom_path);
  settings.save();
}

void GbcImGui::update_bios_path(const std::string &bios_path) {
  const auto new_bios_path = fs::path(bios_path).parent_path().string();
  bios_sel_conf.path = new_bios_path;
  settings.bios_dir = new_bios_path;
  settings.prev_bios_path = bios_path;
  settings.save();
}

/* ImGui windows */
void GbcImGui::build_main_menu_bar(UiState &state) const {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Load ROM..."))
        ImGuiFileDialog::Instance()->OpenDialog(
            "RomFileDialog", "Choose a ROM file", rom_filters.data(),
            rom_sel_conf);

      if (ImGui::BeginMenu("Open Recent")) {
        if (settings.recent_roms.empty()) {
          ImGui::MenuItem("(No recent files)", nullptr, false, false);
        } else {
          int i = 0;
          for (const auto &path : settings.recent_roms) {
            // This solves the identical label problem in ImGui
            ImGui::PushID(i++);
            if (ImGui::MenuItem(
                    std::filesystem::path(path).filename().string().c_str())) {
              state.load_rom_path = path;
              state.request_load_rom = true;
            }
            // In case differentiation is needed, we add a tooltip
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("%s", path.c_str());
            ImGui::PopID();
          }
        }
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Select BIOS"))
        ImGuiFileDialog::Instance()->OpenDialog(
            "BiosFileDialog", "Choose a BIN file", bios_filters.data(),
            bios_sel_conf);
      if (ImGui::MenuItem("Quit"))
        state.request_quit = true;
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Options")) {
      if (ImGui::MenuItem("Settings"))
        state.show_settings = true;
      if (ImGui::MenuItem("Keybinds"))
        state.show_keybinds = true;
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
      if (ImGui::MenuItem("Open Debugger"))
        state.show_debug = true;
      if (ImGui::MenuItem("Edit Breakpoints"))
        state.show_breakpoints = true;
      if (ImGui::MenuItem("Show PPU Viewer"))
        state.show_ppu_viewer = true;
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("About")) {
      if (ImGui::MenuItem("About"))
        state.show_about = true;
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }
}

void GbcImGui::build_status_bar(UiState &state) const {
  const float height = ImGui::GetFrameHeight();
  const ImGuiViewport *viewport = ImGui::GetMainViewport();

  // Position at bottom of the main viewport
  // (Viewport Y + Viewport Height - Bar Height)
  ImGui::SetNextWindowPos(
      ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height));
  // Stretch across the full width
  ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));

  // Style: No rounding, no border, nice padding
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(10.0f * dpi_scale, 2.0f * dpi_scale));

  // Flags: No title bar, no resizing, no moving, no saving settings
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

  if (ImGui::Begin("StatusBar", nullptr, flags)) {
    // --- Left Aligned Content ---
    if (!state.load_rom_path.empty()) {
      ImGui::Text("Loaded: %s", std::filesystem::path(state.load_rom_path)
                                    .filename()
                                    .string()
                                    .c_str());
    } else {
      ImGui::TextDisabled("Ready");
    }

    const float right_items_width = 175.0f * dpi_scale;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - right_items_width);

    if (!state.notifications.empty()) {
      auto highest_level = LogLevel::Debug;
      for (const auto &n : state.notifications) {
        if (n.level == LogLevel::Error) {
          highest_level = LogLevel::Error;
        } else if (n.level == LogLevel::Warning &&
                   highest_level != LogLevel::Error) {
          highest_level = LogLevel::Warning;
        }
      }
      if (highest_level == LogLevel::Error) {
        const auto color = get_level_color(LogLevel::Error);
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              get_darkened_color(color, 0.8));
      } else if (highest_level == LogLevel::Warning) {
        const auto color = get_level_color(LogLevel::Warning);
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              get_darkened_color(color, 0.8));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
      }
      const std::string label =
          "Notif (" + std::to_string(state.notifications.size()) + ")";
      if (ImGui::SmallButton(label.c_str())) {
        state.show_notifications = !state.show_notifications;
      }
      if (highest_level == LogLevel::Error) {
        ImGui::PopStyleColor(2);
      } else if (highest_level == LogLevel::Warning) {
        ImGui::PopStyleColor(3);
      }
      ImGui::SameLine();
    }

    // --- Right aligned stuff ---
    const auto fps_fmt = "FPS: %.1f";
    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), fps_fmt, state.current_fps);

    const float text_width = ImGui::CalcTextSize(fps_text).x;
    const float right_margin = 20.0f * dpi_scale; // Padding from right edge

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - text_width - right_margin);
    ImGui::TextUnformatted(fps_text);

    ImGui::End();
  }

  ImGui::PopStyleVar(3); // Pop Rounding, BorderSize, Padding
}

void GbcImGui::build_file_dialogs(UiState &state) const {
  auto [max_size, min_size] = get_min_dialog_size();
  if (ImGuiFileDialog::Instance()->Display(
          "RomFileDialog", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      state.load_rom_path = ImGuiFileDialog::Instance()->GetFilePathName();
      state.request_load_rom = true;
    }
    ImGuiFileDialog::Instance()->Close();
  }

  if (ImGuiFileDialog::Instance()->Display(
          "BiosFileDialog", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
    if (ImGuiFileDialog::Instance()->IsOk()) {
      const auto bios_path = ImGuiFileDialog::Instance()->GetFilePathName();
      try {
        auto bios_rom = BootROM(bios_path);
        state.load_bios_path = bios_path;
        state.request_load_bios = true;
      } catch (std::runtime_error &e) {
        Logger::push(LogLevel::Warning, "BIOS", "Failed to load BIOS",
                     e.what());
      }
    }
    ImGuiFileDialog::Instance()->Close();
  }
}

void GbcImGui::build_settings_window(UiState &state, SDLHost &host) {
  ImGui::Begin("Settings", &state.show_settings);
  ImGui::SeparatorText("General");
  ImGui::Checkbox("Fast forward", &state.fast_forward);
  ImGui::Checkbox("Force DMG monochrome", &settings.force_mono_dmg);
  ImGui::SeparatorText("Audio");
  // Volume slider
  ImGui::SetNextItemWidth(200.0f * dpi_scale);
  if (ImGui::SliderFloat("Volume", &settings.volume, 0.0f, 1.5f, "%.2f")) {
    host.set_volume(settings.volume);
  }

  // Output device dropdown
  if (state.audio_device_names.empty()) {
    SDLHost::refresh_audio_devices(state.audio_device_names,
                                   state.audio_device_ids);
    state.current_audio_dev_idx = 0;
  }
  ImGui::SetNextItemWidth(150.0f * dpi_scale);

  std::vector<const char *> items;
  items.reserve(state.audio_device_names.size());
  for (auto &s : state.audio_device_names)
    items.push_back(s.c_str());

  const int old_audio_idx = state.current_audio_dev_idx;
  if (ImGui::Combo("Output device", &state.current_audio_dev_idx, items.data(),
                   static_cast<int>(items.size()))) {
    if (!host.set_audio_device(state.current_audio_dev_idx,
                               state.audio_device_ids, settings.volume)) {
      state.current_audio_dev_idx = old_audio_idx; // revert on failure
      host.set_audio_device(old_audio_idx, state.audio_device_ids,
                            settings.volume);
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Refresh")) {
    SDLHost::refresh_audio_devices(state.audio_device_names,
                                   state.audio_device_ids);
    state.current_audio_dev_idx =
        std::min(state.current_audio_dev_idx,
                 static_cast<int>(state.audio_device_names.size()) - 1);
  }

  ImGui::Checkbox("Reduce Pops", &state.reduce_audio_pops);
  ImGui::End();
}

void GbcImGui::build_keybinds_window(UiState &state) {
  ImGui::Begin("Keybinds", &state.show_keybinds);
  ImGui::SeparatorText("Gameplay");
  // Build an array of names for ImGui::Combo
  static std::array<const char *, kPresets.size()> preset_names{};
  static bool preset_names_init = false;
  if (!preset_names_init) {
    for (size_t i = 0; i < kPresets.size(); ++i)
      preset_names[i] = kPresets[i].name;
    preset_names_init = true;
  }

  const int old_idx = settings.keybind_preset_index;
  ImGui::SetNextItemWidth(100.0f * dpi_scale);
  if (ImGui::Combo("Preset", &settings.keybind_preset_index,
                   preset_names.data(), preset_names.size())) {
    // Only apply immediately if not currently rebinding
    if (state.waiting_for_bind < 0) {
      apply_keybind_preset(settings.keybinds, settings.keybind_preset_index);
    } else {
      // revert change while waiting for bind
      settings.keybind_preset_index = old_idx;
    }
  }
  ImGui::Spacing();

  for (std::size_t i = 0; i < control_labels.size(); ++i) {
    ImGui::Text("%s", control_labels[i].data());
    ImGui::SameLine(120.0f * dpi_scale);

    const bool waiting = state.waiting_for_bind == static_cast<int>(i);
    std::string button_label =
        waiting ? "Press a key..."
                : std::string("Bind##") + std::string(control_labels[i]);
    if (ImGui::Button(button_label.c_str()))
      state.waiting_for_bind = static_cast<int>(i);

    ImGui::SameLine(240.0f * dpi_scale);
    ImGui::Text("%s", SDL_GetKeyName(settings.keybinds[i]));
  }

  ImGui::SeparatorText("General");
  for (std::size_t i = 0; i < general_labels.size(); ++i) {
    ImGui::Text("%s", general_labels[i].data());
    ImGui::SameLine(120.0f * dpi_scale);

    const bool waiting = state.waiting_for_bind == static_cast<int>(i + KCount);
    std::string button_label =
        waiting ? "Press a key..."
                : std::string("Bind##") + std::string(general_labels[i]);
    if (ImGui::Button(button_label.c_str()))
      state.waiting_for_bind = static_cast<int>(i + KCount);

    ImGui::SameLine(240.0f * dpi_scale);
    ImGui::Text("%s", SDL_GetKeyName(settings.general_keybinds[i]));
  }

  ImGui::End();
}

void GbcImGui::update_dpi_scale(const float new_scale) {
  ImGui::GetStyle() = ImGuiStyle();
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();

  style.FontScaleDpi = new_scale;
  // Calculate relative change (e.g., moving 1.0 -> 2.0 means factor 2.0)
  const float relative_scale = new_scale / dpi_scale;

  // Scale all padding, rounding, and spacing
  style.ScaleAllSizes(relative_scale);

  if (ImGuiContext *ctx = ImGui::GetCurrentContext()) {
    for (int i = 0; i < ctx->Windows.Size; i++) {
      ImGuiWindow *window = ctx->Windows[i];
      // Rescale the window's size and position
      window->Pos.x *= relative_scale;
      window->Pos.y *= relative_scale;
      window->Size.x *= relative_scale;
      window->Size.y *= relative_scale;
      // Also rescale the "SizeFull" (used for non-collapsed state)
      window->SizeFull.x *= relative_scale;
      window->SizeFull.y *= relative_scale;
    }
  }

  dpi_scale = new_scale;
}

void GbcImGui::build_about_window(UiState &state) {
  // Set a default size and position (bottom right)
  ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("About", &state.show_about)) {
    ImGui::SeparatorText("Source");
    ImGui::Text("%s", "github.com/AlexSutila/GBCEmulator");
    ImGui::SeparatorText("Cartridge Info");
    ImGui::Text("%s", state.cart_info.c_str());
  }
  ImGui::End();
}

void GbcImGui::build_notification_window(UiState &state) const {
  // Set a default size and position (bottom right)
  ImGui::SetNextWindowSize(ImVec2(400 * dpi_scale, 300 * dpi_scale),
                           ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Notifications", &state.show_notifications)) {
    // --- Header / Toolbar ---
    if (state.notifications.empty()) {
      ImGui::TextDisabled("No new notifications.");
    } else {
      if (ImGui::Button("Clear All")) {
        state.notifications.clear();
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%zu messages", state.notifications.size());
      ImGui::Separator();
    }
    // --- List of Messages ---
    // We need to track deletion ID because we can't erase from vector while
    // looping
    int id_to_delete = -1;
    // Iterate backwards so newest are at the top
    for (auto &n : std::ranges::reverse_view(state.notifications)) {
      ImGui::PushID(n.id);
      // Color coding
      ImGui::PushStyleColor(ImGuiCol_Text, get_level_color(n.level));
      // Summary Line (Expandable)
      // Format: [Type] Summary
      std::string header = "[" + n.type + "] " + n.summary;
      const bool open = ImGui::TreeNode("##Node", "%s", header.c_str());

      ImGui::PopStyleColor(); // Restore text color

      // Details (if expanded)
      if (open) {
        ImGui::Indent();
        ImGui::TextWrapped("%s", n.details.c_str());
        // Timestamp
        char time_buf[64];
        std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S",
                      std::localtime(&n.timestamp));
        ImGui::TextDisabled("Time: %s", time_buf);

        if (ImGui::Button("Dismiss")) {
          id_to_delete = n.id;
        }
        ImGui::Unindent();
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    // Handle deletion safely outside the loop
    if (id_to_delete != -1) {
      std::erase_if(state.notifications, [id_to_delete](const Notification &n) {
        return n.id == id_to_delete;
      });
    }
    ImGui::End();
  }
}

void GbcImGui::apply_keybind_preset(std::array<SDL_Keycode, 8> &array,
                                    const int keybind_preset_index) {
  if (keybind_preset_index < 0 ||
      keybind_preset_index >= static_cast<int>(kPresets.size()))
    return;
  if (keybind_preset_index == kCustomPresetIndex)
    return; // don't clobber custom
  array = kPresets[keybind_preset_index].keys;
}

std::tuple<ImVec2, ImVec2> GbcImGui::get_min_dialog_size() const {
  const float display_w = ImGui::GetIO().DisplaySize.x;
  const float display_h = ImGui::GetIO().DisplaySize.y;
  return std::make_tuple(ImVec2(display_w, display_h),
                         ImVec2(400.0f * dpi_scale, 250.0f * dpi_scale));
}

ImVec4 GbcImGui::get_darkened_color(const ImVec4 color, const float factor) {
  return {std::max(0.0f, color.x * factor), std::max(0.0f, color.y * factor),
          std::max(0.0f, color.z * factor), color.w};
}

ImVec4 GbcImGui::get_level_color(const LogLevel level) {
  switch (level) {
  case LogLevel::Error:
    return {0.80f, 0.40f, 0.40f, 1.0f}; // Light red #cc6666
  case LogLevel::Warning:
    return {0.94f, 0.78f, 0.45f, 1.0f}; // Yellow #f0c674
  case LogLevel::Info:
    return {0.71f, 0.74f, 0.40f, 1.0f}; // Light green #b5bd68
  default:
    return {0.77f, 0.78f, 0.78f, 1.0f}; // Grey #c5c8c6
  }
}
