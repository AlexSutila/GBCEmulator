#include "frontend/sdl3/gui.hpp"
#include "memory/boot.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct TextInputCache {
  std::array<char, 512> buffer{};
  std::string synced_value;
};

void sync_text_input_cache(TextInputCache &cache, const std::string &value) {
  if (cache.synced_value == value) {
    return;
  }

  std::snprintf(cache.buffer.data(), cache.buffer.size(), "%s", value.c_str());
  cache.synced_value = value;
}

void build_resettable_path_input(const float dpi_scale, const char *label, const char *reset_label,
                                 const char *helper_text, const char *default_value,
                                 std::string &value, TextInputCache &cache) {
  sync_text_input_cache(cache, value);

  ImGui::SetNextItemWidth(320.0f * dpi_scale);
  if (ImGui::InputText(label, cache.buffer.data(), cache.buffer.size())) {
    value = cache.buffer.data();
    cache.synced_value = value;
  }

  ImGui::SameLine();
  if (ImGui::Button(reset_label)) {
    value = default_value;
    sync_text_input_cache(cache, value);
  }
  ImGui::TextDisabled("%s", helper_text);
}

LogLevel highest_notification_level(const UiState &state) {
  auto highest_level = LogLevel::Debug;
  for (const auto &notification : state.notifications) {
    if (notification.level == LogLevel::Error) {
      return LogLevel::Error;
    }
    if (notification.level == LogLevel::Warning) {
      highest_level = LogLevel::Warning;
    }
  }
  return highest_level;
}
} // namespace

void GbcImGui::build_main_menu_bar(UiState &state) const {
  state.menu_bar_height = ImGui::GetFrameHeight();
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }

  state.menu_bar_height = ImGui::GetWindowHeight();

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Load ROM...")) {
      ImGuiFileDialog::Instance()->OpenDialog("RomFileDialog", "Choose a ROM file",
                                              rom_filters.data(), rom_sel_conf);
    }
    if (ImGui::MenuItem("Load from URL...")) {
      state.show_load_url_popup = true;
    }

    if (ImGui::BeginMenu("Open Recent")) {
      if (settings.recent_roms.empty()) {
        ImGui::MenuItem("(No recent files)", nullptr, false, false);
      } else {
        int menu_index = 0;
        for (const auto &path : settings.recent_roms) {
          ImGui::PushID(menu_index++);
          if (ImGui::MenuItem(fs::path(path).filename().string().c_str())) {
            state.load_rom_path = path;
            state.load_rom_name.clear();
            state.request_load_rom = true;
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", path.c_str());
          }
          ImGui::PopID();
        }
      }
      ImGui::EndMenu();
    }

    const bool bios_loaded = !settings.prev_bios_path.empty();
    const std::string bios_name =
        bios_loaded ? fs::path(settings.prev_bios_path).filename().string() : "None";
    if (ImGui::BeginMenu("BIOS")) {
      ImGui::TextDisabled("Current: %s", bios_name.c_str());
      if (bios_loaded && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", settings.prev_bios_path.c_str());
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Select BIOS...")) {
        ImGuiFileDialog::Instance()->OpenDialog("BiosFileDialog", "Choose a BIN file",
                                                bios_filters.data(), bios_sel_conf);
      }
      if (!bios_loaded) {
        ImGui::BeginDisabled();
      }
      if (ImGui::MenuItem("Unload BIOS")) {
        state.request_unload_bios = true;
      }
      if (!bios_loaded) {
        ImGui::EndDisabled();
      }
      ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Quit")) {
      state.request_quit = true;
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Options")) {
    if (ImGui::MenuItem("Settings")) {
      state.show_settings = true;
    }
    if (ImGui::MenuItem("Cheats")) {
      state.show_cheats = true;
    }
    if (ImGui::MenuItem("Keybinds")) {
      state.show_keybinds = true;
    }
    if (ImGui::MenuItem("Save States")) {
      state.show_savestate_manager = true;
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Debug")) {
    if (ImGui::MenuItem("Open Debugger")) {
      state.show_main_debug_viewer = true;
    }
    if (ImGui::MenuItem("Edit Breakpoints")) {
      state.show_breakpoints = true;
    }
    if (ImGui::MenuItem("Show Memory Viewer")) {
      state.show_memory_viewer = true;
    }
    if (ImGui::MenuItem("Show PPU Viewer")) {
      state.show_ppu_viewer = true;
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("About")) {
    if (ImGui::MenuItem("About")) {
      state.show_about = true;
    }
    if (ImGui::MenuItem("Cartridge Info")) {
      state.show_cart_info = true;
    }
    ImGui::EndMenu();
  }

  ImGui::EndMainMenuBar();
}

void GbcImGui::build_status_bar(UiState &state) const {
  const float height = ImGui::GetFrameHeight();
  state.status_bar_height = height;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const Uint64 now_ticks = SDL_GetTicks();
  if (!state.transient_status_text.empty() && now_ticks >= state.transient_status_until_ticks) {
    state.transient_status_text.clear();
    state.transient_status_details.clear();
  }

  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height));
  ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * dpi_scale, 2.0f * dpi_scale));

  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoNav;

  if (ImGui::Begin("StatusBar", nullptr, flags)) {
    state.status_bar_height = ImGui::GetWindowHeight();
    if (state.io_busy) {
      if (state.io_progress >= 0.0f) {
        ImGui::Text("%s (%.0f%%)", state.io_status.c_str(), state.io_progress * 100.0f);
      } else {
        ImGui::Text("%s", state.io_status.c_str());
      }
    } else if (!state.transient_status_text.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, get_level_color(state.transient_status_level));
      ImGui::Text("%s", state.transient_status_text.c_str());
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered() && !state.transient_status_details.empty() &&
          state.transient_status_details != state.transient_status_text) {
        ImGui::SetTooltip("%s", state.transient_status_details.c_str());
      }
    } else if (!state.load_rom_path.empty()) {
      ImGui::Text("Loaded: %s", state.load_rom_name.c_str());
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", state.load_rom_path.c_str());
      }
    } else {
      ImGui::TextDisabled("Ready");
    }

    const bool bios_loaded = !settings.prev_bios_path.empty();
    const std::string bios_name =
        bios_loaded ? fs::path(settings.prev_bios_path).filename().string() : "None";
    ImGui::SameLine();
    ImGui::TextDisabled("| BIOS: %s", bios_name.c_str());
    if (bios_loaded && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", settings.prev_bios_path.c_str());
    }

    const float right_items_width = 175.0f * dpi_scale;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - right_items_width);

    if (!state.notifications.empty()) {
      int color_count = 0;
      const auto highest_level = highest_notification_level(state);
      if (highest_level == LogLevel::Error) {
        const auto color = get_level_color(LogLevel::Error);
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, get_darkened_color(color, 0.8f));
        color_count = 2;
      } else if (highest_level == LogLevel::Warning) {
        const auto color = get_level_color(LogLevel::Warning);
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, get_darkened_color(color, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        color_count = 3;
      }

      const std::string label = "Notif (" + std::to_string(state.notifications.size()) + ")";
      if (ImGui::SmallButton(label.c_str())) {
        state.show_notifications = !state.show_notifications;
      }
      if (color_count > 0) {
        ImGui::PopStyleColor(color_count);
      }
      ImGui::SameLine();
    }

    char fps_text[32];
    std::snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", state.current_fps);
    const float text_width = ImGui::CalcTextSize(fps_text).x;
    const float right_margin = 20.0f * dpi_scale;

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - text_width - right_margin);
    ImGui::TextUnformatted(fps_text);

    ImGui::End();
  }

  ImGui::PopStyleVar(3);
}

void GbcImGui::build_file_dialogs(UiState &state) const {
  auto [max_size, min_size] = get_min_dialog_size();
  const auto display_dialog = [&](const char *dialog_id, auto &&on_accept) {
    if (ImGuiFileDialog::Instance()->Display(dialog_id, ImGuiWindowFlags_NoCollapse, min_size,
                                             max_size)) {
      if (ImGuiFileDialog::Instance()->IsOk()) {
        on_accept();
      }
      ImGuiFileDialog::Instance()->Close();
    }
  };

  display_dialog("RomFileDialog", [&] {
    state.load_rom_path = ImGuiFileDialog::Instance()->GetFilePathName();
    state.load_rom_name.clear();
    state.request_load_rom = true;
  });

  display_dialog("BiosFileDialog", [&] {
    const auto bios_path = ImGuiFileDialog::Instance()->GetFilePathName();
    try {
      auto bios_rom = BootROM(bios_path);
      (void)bios_rom;
      state.load_bios_path = bios_path;
      state.request_load_bios = true;
    } catch (const std::runtime_error &e) {
      Logger::push(LogLevel::Warning, "BIOS", "Failed to load BIOS", e.what());
    }
  });
}

void GbcImGui::build_rom_source_window(UiState &state) const {
  if (state.show_load_url_popup) {
    ImGui::OpenPopup("Load ROM/ZIP from URL");
    state.show_load_url_popup = false;
  }

  if (ImGui::BeginPopupModal("Load ROM/ZIP from URL", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Enter a link to a .gb/.gbc ROM or a .zip archive");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(520.0f * dpi_scale);
    ImGui::InputTextWithHint("##rom_url", "https://example.com/game.zip", state.load_url_input,
                             IM_ARRAYSIZE(state.load_url_input));

    const bool can_load = state.load_url_input[0] != '\0';
    if (!can_load) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Load")) {
      state.load_rom_path = state.load_url_input;
      state.load_rom_name.clear();
      state.request_load_rom = true;
      ImGui::CloseCurrentPopup();
    }
    if (!can_load) {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (state.show_zip_picker_popup) {
    ImGui::OpenPopup("Choose ROM from ZIP");
  }

  if (ImGui::BeginPopupModal("Choose ROM from ZIP", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!state.zip_picker_title.empty()) {
      ImGui::TextUnformatted(state.zip_picker_title.c_str());
    } else {
      ImGui::TextUnformatted("Multiple ROM files were found in this ZIP");
    }
    ImGui::Spacing();

    const float list_box_width = 520.0f * dpi_scale;
    const float list_box_height = 220.0f * dpi_scale;
    if (ImGui::BeginListBox("##zip_rom_list", ImVec2(list_box_width, list_box_height))) {
      for (int i = 0; i < static_cast<int>(state.zip_rom_entries.size()); ++i) {
        const bool selected = (i == state.zip_rom_selected_idx);
        if (ImGui::Selectable(state.zip_rom_entries[i].c_str(), selected)) {
          state.zip_rom_selected_idx = i;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndListBox();
    }

    const bool has_entries = !state.zip_rom_entries.empty();
    if (!has_entries) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("OK")) {
      state.zip_picker_action = 1;
      state.show_zip_picker_popup = false;
      ImGui::CloseCurrentPopup();
    }
    if (!has_entries) {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      state.zip_picker_action = 2;
      state.show_zip_picker_popup = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

void GbcImGui::build_settings_window(UiState &state, SDLHost &host) {
  ImGuiWindowFlags flags = 0;
  const bool fill_viewport = rendering_detached_dialog(DialogId::Settings);
  if (fill_viewport) {
    setup_full_viewport_window(flags);
  }

  ImGui::Begin("Settings", &state.show_settings, flags);
  ImGui::SeparatorText("General");
  ImGui::Checkbox("Fast forward", &state.fast_forward);
  ImGui::Checkbox("Force DMG monochrome", &settings.force_mono_dmg);

  // ImGui text widgets need stable mutable buffers, so these caches mirror the
  // current settings values and are re-synced only when the backing strings
  // change outside this widget.
  static TextInputCache save_root_cache;
  static TextInputCache savestate_root_cache;
  static TextInputCache cheat_root_cache;
  build_resettable_path_input(dpi_scale, "Save dir", "Reset##save_root",
                              "Saves: game-name - checksum.sav", "./saves", settings.save_root_dir,
                              save_root_cache);
  build_resettable_path_input(dpi_scale, "Savestate dir", "Reset##savestate_root",
                              "Savestate folders: game-name - checksum", "./savestates",
                              settings.savestate_root_dir, savestate_root_cache);
  build_resettable_path_input(dpi_scale, "Cheat dir", "Reset##cheat_root",
                              "Cheats: game-name - checksum.cht", "./cheats",
                              settings.cheat_root_dir, cheat_root_cache);

  ImGui::SetNextItemWidth(120.0f * dpi_scale);
  if (ImGui::InputInt("Max quicksaves", &settings.max_quicksaves) && settings.max_quicksaves < 0) {
    settings.max_quicksaves = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset##max_quicksaves")) {
    settings.max_quicksaves = Settings::default_max_quicksaves;
  }
  ImGui::TextDisabled("0 = unlimited");

  ImGui::SeparatorText("Audio");
  ImGui::SetNextItemWidth(200.0f * dpi_scale);
  if (ImGui::SliderFloat("Volume", &settings.volume, 0.0f, 1.5f, "%.2f")) {
    host.set_volume(settings.volume);
  }

  if (state.audio_device_names.empty()) {
    SDLHost::refresh_audio_devices(state.audio_device_names, state.audio_device_ids);
    state.current_audio_dev_idx = 0;
  }

  ImGui::SetNextItemWidth(150.0f * dpi_scale);
  std::vector<const char *> items;
  items.reserve(state.audio_device_names.size());
  for (auto &name : state.audio_device_names) {
    items.push_back(name.c_str());
  }

  const int old_audio_idx = state.current_audio_dev_idx;
  if (ImGui::Combo("Output device", &state.current_audio_dev_idx, items.data(),
                   static_cast<int>(items.size()))) {
    if (!host.set_audio_device(state.current_audio_dev_idx, state.audio_device_ids,
                               settings.volume)) {
      state.current_audio_dev_idx = old_audio_idx;
      host.set_audio_device(old_audio_idx, state.audio_device_ids, settings.volume);
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Refresh")) {
    SDLHost::refresh_audio_devices(state.audio_device_names, state.audio_device_ids);
    state.current_audio_dev_idx = std::min(state.current_audio_dev_idx,
                                           static_cast<int>(state.audio_device_names.size()) - 1);
  }

  ImGui::End();
  teardown_full_viewport_window(fill_viewport);
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
    return {0.80f, 0.40f, 0.40f, 1.0f};
  case LogLevel::Warning:
    return {0.94f, 0.78f, 0.45f, 1.0f};
  case LogLevel::Status:
  case LogLevel::Info:
    return {0.71f, 0.74f, 0.40f, 1.0f};
  default:
    return {0.77f, 0.78f, 0.78f, 1.0f};
  }
}
