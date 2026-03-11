#include "frontend/sdl3/gui.hpp"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <misc/cpp/imgui_stdlib.h>
#include <ranges>
#include <sstream>

namespace {
std::string format_timestamp_local_gui(const std::time_t timestamp) {
  if (timestamp <= 0) {
    return "Unknown";
  }

  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &timestamp);
#else
  localtime_r(&timestamp, &tm);
#endif

  std::ostringstream stream;
  stream << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return stream.str();
}

std::optional<std::vector<std::uint32_t>>
read_thumb_raw_argb_gui(const std::filesystem::path &path, const int width, const int height) {
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }

  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const std::size_t byte_count = pixel_count * sizeof(std::uint32_t);
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return std::nullopt;
  }
  if (const auto size = static_cast<std::size_t>(file.tellg()); size != byte_count) {
    return std::nullopt;
  }

  std::vector<std::uint32_t> out(pixel_count);
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(byte_count))) {
    return std::nullopt;
  }
  return out;
}

constexpr std::array kCheatFormatLabels{"Auto detect", "GameShark/Xploder", "Game Genie",
                                        "Raw (addr[?cmp]:value)", "CodeBreaker"};

std::string cheat_display_name(const Settings::CheatEntry &entry, const std::size_t index) {
  if (!entry.name.empty()) {
    return entry.name;
  }
  if (!entry.code.empty()) {
    return entry.code;
  }
  return "Cheat " + std::to_string(index + 1);
}
} // namespace

void GbcImGui::build_cheats_window(UiState &state) {
  ImGuiWindowFlags flags = 0;
  const bool fill_viewport = rendering_detached_dialog(DialogId::Cheats);
  if (fill_viewport) {
    setup_full_viewport_window(flags);
  } else {
    ImGui::SetNextWindowSize(ImVec2(860.0f * dpi_scale, 520.0f * dpi_scale),
                             ImGuiCond_FirstUseEver);
  }

  if (!ImGui::Begin("Cheats", &state.show_cheats, flags)) {
    ImGui::End();
    teardown_full_viewport_window(fill_viewport);
    return;
  }

  bool settings_dirty = false;
  auto &cheats = settings.cheats;
  if (state.selected_cheat_idx >= static_cast<int>(cheats.size())) {
    state.selected_cheat_idx = cheats.empty() ? -1 : static_cast<int>(cheats.size()) - 1;
  }

  ImGui::Separator();
  if (ImGui::Button("Add")) {
    Settings::CheatEntry entry;
    entry.name = "Cheat " + std::to_string(cheats.size() + 1);
    cheats.push_back(std::move(entry));
    state.selected_cheat_idx = static_cast<int>(cheats.size()) - 1;
    settings_dirty = true;
  }

  ImGui::SameLine();
  const bool can_edit_selected =
      state.selected_cheat_idx >= 0 && state.selected_cheat_idx < static_cast<int>(cheats.size());
  if (!can_edit_selected) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Duplicate")) {
    cheats.push_back(cheats[static_cast<std::size_t>(state.selected_cheat_idx)]);
    state.selected_cheat_idx = static_cast<int>(cheats.size()) - 1;
    settings_dirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete")) {
    cheats.erase(cheats.begin() + state.selected_cheat_idx);
    if (cheats.empty()) {
      state.selected_cheat_idx = -1;
    } else if (state.selected_cheat_idx >= static_cast<int>(cheats.size())) {
      state.selected_cheat_idx = static_cast<int>(cheats.size()) - 1;
    }
    settings_dirty = true;
  }
  if (!can_edit_selected) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (ImGui::Button("Enable All")) {
    for (auto &entry : cheats) {
      entry.enabled = true;
    }
    settings_dirty = !cheats.empty();
  }

  ImGui::SameLine();
  if (ImGui::Button("Disable All")) {
    for (auto &entry : cheats) {
      entry.enabled = false;
    }
    settings_dirty = !cheats.empty();
  }

  ImGui::Spacing();
  if (ImGui::BeginTable("cheat_layout", 2,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Cheat List", ImGuiTableColumnFlags_WidthStretch, 0.43f);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch, 0.57f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginTable("cheat_entries", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 0.0f))) {
      ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 40.0f * dpi_scale);
      ImGui::TableSetupColumn("Cheat", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      for (std::size_t i = 0; i < cheats.size(); ++i) {
        auto &entry = cheats[i];
        const bool selected = state.selected_cheat_idx == static_cast<int>(i);

        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox("##enabled", &entry.enabled)) {
          settings_dirty = true;
        }

        ImGui::TableSetColumnIndex(1);
        if (const auto label = cheat_display_name(entry, i);
            ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
          state.selected_cheat_idx = static_cast<int>(i);
        }
        if (ImGui::IsItemHovered() && !entry.code.empty()) {
          ImGui::SetTooltip("%s", entry.code.c_str());
        }
        ImGui::PopID();
      }

      if (cheats.empty()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("-");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("No cheats added");
      }
      ImGui::EndTable();
    }

    ImGui::TableSetColumnIndex(1);
    const bool has_selection =
        state.selected_cheat_idx >= 0 && state.selected_cheat_idx < static_cast<int>(cheats.size());
    if (!has_selection) {
      ImGui::TextDisabled("Select a cheat from the list to edit.\nCheat codes are tied to ROMs.");
    } else {
      auto &entry = cheats[static_cast<std::size_t>(state.selected_cheat_idx)];

      if (ImGui::InputText("Name", &entry.name)) {
        settings_dirty = true;
      }
      if (ImGui::InputText("Code", &entry.code)) {
        settings_dirty = true;
      }

      int format = std::clamp(entry.format, 0, static_cast<int>(kCheatFormatLabels.size()) - 1);
      if (format != entry.format) {
        entry.format = format;
        settings_dirty = true;
      }
      if (ImGui::Combo("Format", &format, kCheatFormatLabels.data(),
                       static_cast<int>(kCheatFormatLabels.size()))) {
        entry.format = format;
        settings_dirty = true;
      }

      if (ImGui::Checkbox("Enabled##editor", &entry.enabled)) {
        settings_dirty = true;
      }
      ImGui::Text("Notes");
      if (ImGui::InputTextMultiline("Notes", &entry.notes, ImVec2(-1.0f, 180.0f * dpi_scale))) {
        settings_dirty = true;
      }

      ImGui::TextDisabled("Compare is supported in Game Genie and Raw (AAAA?CC:VV).");
    }

    ImGui::EndTable();
  }

  if (settings_dirty) {
    state.cheats_dirty = true;
    state.cheats_file_dirty = true;
  }
  ImGui::End();
  teardown_full_viewport_window(fill_viewport);
}

void GbcImGui::build_savestate_manager_window(
    UiState &state, SDL_Renderer *renderer, const bool emulator_ready,
    const std::filesystem::path &savestate_dir, std::array<char, 96> &manual_label_input,
    std::vector<SavestateEntry> &savestate_entries,
    std::optional<std::filesystem::path> &savestate_selected_path,
    const SavestateManagerCallbacks &callbacks, const bool fill_viewport) {
  if (!state.show_savestate_manager) {
    return;
  }

  ImGuiWindowFlags flags = 0;
  if (fill_viewport) {
    setup_full_viewport_window(flags);
  } else {
    ImGui::SetNextWindowSize(ImVec2(920, 560), ImGuiCond_FirstUseEver);
  }

  if (!ImGui::Begin("Save States", &state.show_savestate_manager, flags)) {
    ImGui::End();
    teardown_full_viewport_window(fill_viewport);
    return;
  }

  if (savestate_dir.empty()) {
    ImGui::TextDisabled("Load a ROM to manage savestates.");
    ImGui::End();
    teardown_full_viewport_window(fill_viewport);
    return;
  }

  ImGui::Text("Directory: %s", savestate_dir.string().c_str());
  ImGui::InputText("Label", manual_label_input.data(), manual_label_input.size());

  if (!emulator_ready) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Save") && callbacks.queue_manual_save) {
    callbacks.queue_manual_save(manual_label_input.data());
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Most Recent") && callbacks.request_load_most_recent) {
    callbacks.request_load_most_recent();
  }
  if (!emulator_ready) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (ImGui::Button("Refresh") && callbacks.refresh) {
    callbacks.refresh();
  }

  ImGui::Separator();
  std::optional<std::filesystem::path> delete_path;
  std::optional<std::filesystem::path> load_path;

  if (ImGui::BeginTable("savestate_manager_layout", 2,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginTable("savestate_table", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 0.0f))) {
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
      ImGui::TableSetupColumn("Taken", ImGuiTableColumnFlags_WidthFixed, 170.0f);
      ImGui::TableSetupColumn("Label");
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableHeadersRow();

      for (std::size_t i = 0; i < savestate_entries.size(); ++i) {
        auto &entry = savestate_entries[i];
        const bool selected =
            savestate_selected_path.has_value() && *savestate_selected_path == entry.state_path;

        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Selectable(entry.kind.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
          savestate_selected_path = entry.state_path;
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(format_timestamp_local_gui(entry.created_at).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(entry.label.c_str());
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", entry.state_path.filename().string().c_str());
        }
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%zu KB", static_cast<std::size_t>((entry.file_size + 1023u) / 1024u));
        ImGui::PopID();
      }
      ImGui::EndTable();
    }

    ImGui::TableSetColumnIndex(1);
    auto selected_it = savestate_entries.end();
    if (savestate_selected_path.has_value()) {
      selected_it = std::ranges::find_if(savestate_entries, [&](const SavestateEntry &e) {
        return e.state_path == *savestate_selected_path;
      });
    }

    if (selected_it == savestate_entries.end()) {
      ImGui::TextDisabled("No savestate selected.");
    } else {
      auto &entry = *selected_it;
      ImGui::Text("Type: %s", entry.kind.c_str());
      ImGui::Text("Taken: %s", format_timestamp_local_gui(entry.created_at).c_str());
      ImGui::Text("File: %s", entry.state_path.filename().string().c_str());
      ImGui::Text("Size: %zu bytes", static_cast<std::size_t>(entry.file_size));
      ImGui::Spacing();

      if (!emulator_ready) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Load")) {
        load_path = entry.state_path;
      }
      if (!emulator_ready) {
        ImGui::EndDisabled();
      }
      ImGui::SameLine();
      if (ImGui::Button("Delete")) {
        delete_path = entry.state_path;
      }

      ImGui::SeparatorText("Thumbnail");
      if (entry.thumb_texture && entry.thumb_renderer != renderer) {
        // Thumbnails are cached as SDL textures, so they are renderer-owned.
        // If the savestate window moves between attached/detached renderers, the
        // old texture must be dropped before we can lazily rebuild it
        SDL_DestroyTexture(entry.thumb_texture);
        entry.thumb_texture = nullptr;
        entry.thumb_texture_attempted = false;
        entry.thumb_renderer = nullptr;
      }
      if (renderer && !entry.thumb_texture && !entry.thumb_texture_attempted) {
        entry.thumb_texture_attempted = true;
        if (std::filesystem::exists(entry.thumb_path)) {
          if (const auto pixels =
                  read_thumb_raw_argb_gui(entry.thumb_path, entry.thumb_w, entry.thumb_h);
              pixels.has_value()) {
            entry.thumb_texture =
                SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                  entry.thumb_w, entry.thumb_h);
            if (entry.thumb_texture) {
              entry.thumb_renderer = renderer;
              SDL_UpdateTexture(entry.thumb_texture, nullptr, pixels->data(),
                                entry.thumb_w * static_cast<int>(sizeof(std::uint32_t)));
              SDL_SetTextureScaleMode(entry.thumb_texture, SDL_SCALEMODE_NEAREST);
            }
          }
        }
      }

      if (entry.thumb_texture) {
        constexpr float max_width = 260.0f;
        const float scale = std::min(max_width / static_cast<float>(entry.thumb_w), 4.0f);
        ImGui::Image(entry.thumb_texture, ImVec2(static_cast<float>(entry.thumb_w) * scale,
                                                 static_cast<float>(entry.thumb_h) * scale));
      } else {
        ImGui::TextDisabled("No thumbnail available.");
      }
    }

    ImGui::EndTable();
  }

  if (load_path.has_value() && callbacks.queue_load) {
    callbacks.queue_load(*load_path);
  }
  if (delete_path.has_value() && callbacks.delete_state) {
    callbacks.delete_state(*delete_path);
  }

  ImGui::End();
  teardown_full_viewport_window(fill_viewport);
}

void GbcImGui::build_keybinds_window(UiState &state) {
  ImGuiWindowFlags flags = 0;
  const bool fill_viewport = rendering_detached_dialog(DialogId::Keybinds);
  if (fill_viewport) {
    setup_full_viewport_window(flags);
  }

  ImGui::Begin("Keybinds", &state.show_keybinds, flags);
  ImGui::SeparatorText("Gameplay");

  static std::array<const char *, kPresets.size()> preset_names{};
  static bool preset_names_init = false;
  if (!preset_names_init) {
    for (std::size_t i = 0; i < kPresets.size(); ++i) {
      preset_names[i] = kPresets[i].name;
    }
    preset_names_init = true;
  }

  const int old_idx = settings.keybind_preset_index;
  ImGui::SetNextItemWidth(100.0f * dpi_scale);
  if (ImGui::Combo("Preset", &settings.keybind_preset_index, preset_names.data(),
                   static_cast<int>(preset_names.size()))) {
    if (!state.waiting_for_bind.has_value()) {
      apply_keybind_preset(settings.keybinds, settings.keybind_preset_index);
    } else {
      settings.keybind_preset_index = old_idx;
    }
  }
  ImGui::Spacing();

  const auto draw_bind_rows = [this, &state](const auto &labels, auto &bindings,
                                             const std::size_t base_index) {
    for (std::size_t i = 0; i < labels.size(); ++i) {
      ImGui::Text("%s", labels[i].data());
      ImGui::SameLine(120.0f * dpi_scale);

      const std::size_t bind_index = base_index + i;
      const bool waiting = state.waiting_for_bind == bind_index;
      const std::string button_label =
          waiting ? "Press a key..." : std::string("Bind##") + std::string(labels[i]);
      if (ImGui::Button(button_label.c_str())) {
        state.waiting_for_bind = bind_index;
      }

      ImGui::SameLine(240.0f * dpi_scale);
      ImGui::Text("%s", SDL_GetKeyName(bindings[i]));
    }
  };

  draw_bind_rows(control_labels, settings.keybinds, 0);

  ImGui::SeparatorText("General");
  draw_bind_rows(general_labels, settings.general_keybinds, KCount);

  ImGui::End();
  teardown_full_viewport_window(fill_viewport);
}

void GbcImGui::build_about_window(UiState &state) {
  ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("About", &state.show_about)) {
    ImGui::SeparatorText("Project");
    ImGui::TextUnformatted("IroGB");
    ImGui::TextLinkOpenURL("https://kaze.moe/TismForge/IroGB", "https://kaze.moe/TismForge/IroGB");

    ImGui::SeparatorText("Authors");
    ImGui::BulletText("Alex Sutila");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("https://github.com/alexsutila", "https://github.com/alexsutila");
    ImGui::BulletText("Xuanli Lin");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("https://github.com/kazum1kun", "https://github.com/kazum1kun");

    ImGui::SeparatorText("Credits");
    ImGui::TextWrapped("We would like to thank the following open source projects for "
                       "providing tools and resources that were instrumental in the "
                       "development of IroGB:");
    populate_credits();

    ImGui::SeparatorText("License");
    ImGui::Text("IroGB is licensed under the GPLv3 License. See ");
    ImGui::TextLinkOpenURL("LICENSE",
                           "https://kaze.moe/TismForge/IroGB/raw/branch/release/LICENSE");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Text(" in the repository for details.");
  }
  ImGui::End();
}

void GbcImGui::build_cart_info_window(UiState &state) {
  ImGui::SetNextWindowSize(ImVec2(600, 440), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Cartridge Info", &state.show_cart_info)) {
    if (state.cart_info.empty()) {
      ImGui::TextDisabled("No cartridge info available");
    } else {
      ImGui::BeginChild("CartInfoScroll", ImVec2(0, 0), true);
      ImGui::TextUnformatted(state.cart_info.c_str());
      ImGui::EndChild();
    }
  }
  ImGui::End();
}

void GbcImGui::build_notification_window(UiState &state) const {
  ImGui::SetNextWindowSize(ImVec2(400 * dpi_scale, 300 * dpi_scale), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Notifications", &state.show_notifications)) {
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

    int id_to_delete = -1;
    for (auto &notification : std::ranges::reverse_view(state.notifications)) {
      ImGui::PushID(notification.id);
      ImGui::PushStyleColor(ImGuiCol_Text, get_level_color(notification.level));
      const std::string header = "[" + notification.type + "] " + notification.summary;
      const bool open = ImGui::TreeNode("##Node", "%s", header.c_str());
      ImGui::PopStyleColor();

      if (open) {
        ImGui::Indent();
        ImGui::TextWrapped("%s", notification.details.c_str());
        ImGui::TextDisabled("Time: %s", format_timestamp_local_gui(notification.timestamp).c_str());
        if (ImGui::Button("Dismiss")) {
          id_to_delete = notification.id;
        }
        ImGui::Unindent();
        ImGui::TreePop();
      }
      ImGui::PopID();
    }

    if (id_to_delete != -1) {
      std::erase_if(state.notifications, [id_to_delete](const Notification &notification) {
        return notification.id == id_to_delete;
      });
    }
    ImGui::End();
  }
}

void GbcImGui::apply_keybind_preset(std::array<SDL_Keycode, 8> &array,
                                    const int keybind_preset_index) {
  if (keybind_preset_index < 0 || keybind_preset_index >= static_cast<int>(kPresets.size()) ||
      keybind_preset_index == kCustomPresetIndex) {
    return;
  }
  array = kPresets[keybind_preset_index].keys;
}

void GbcImGui::populate_credits() {
  for (const auto &[name, url, license] : kThirdPartyProjects) {
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextLinkOpenURL(name.data(), url.data());
    ImGui::SameLine();
    ImGui::Text("%s", license.data());
  }
}
