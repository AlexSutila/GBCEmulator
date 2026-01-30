#include "frontend/sdl3/gui.hpp"
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <sys/stat.h>

#include "memory/boot.hpp"

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

  rom_sel_conf.path = settings.rom_dir;
  rom_sel_conf.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ReadOnlyFileNameField;
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
  build_main_menu_bar(state);
  build_file_dialogs(state);
  if (state.show_settings)
    build_settings_window(state, host);
  if (state.show_keybinds)
    build_keybinds_window(state);
  if (state.show_bios_error)
    build_bios_error_popup(state, state.bios_error_message);
}

// Returns true if the event was handled by the GUI and should be ignored by the
// game
bool GbcImGui::process_event(const SDL_Event &e, UiState &ui_state) {
  ImGui_ImplSDL3_ProcessEvent(&e);

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
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void GbcImGui::build_file_dialogs(UiState &state) {
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
        state.bios_error_message = e.what();
        state.show_bios_error = true;
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
  ImGui::SetNextItemWidth(200.0f);
  if (ImGui::SliderFloat("Volume", &settings.volume, 0.0f, 1.5f, "%.2f")) {
    host.set_volume(settings.volume);
  }

  // Output device dropdown
  if (state.audio_device_names.empty()) {
    SDLHost::refresh_audio_devices(state.audio_device_names,
                                   state.audio_device_ids);
    state.current_audio_dev_idx = 0;
  }
  ImGui::SetNextItemWidth(260.0f);

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
  ImGui::SetNextItemWidth(100.0f);
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
    ImGui::SameLine(120.0f);

    const bool waiting = state.waiting_for_bind == static_cast<int>(i);
    std::string button_label =
        waiting ? "Press a key..."
                : std::string("Bind##") + std::string(control_labels[i]);
    if (ImGui::Button(button_label.c_str()))
      state.waiting_for_bind = static_cast<int>(i);

    ImGui::SameLine(240.0f);
    ImGui::Text("%s", SDL_GetKeyName(settings.keybinds[i]));
  }

  ImGui::SeparatorText("General");
  for (std::size_t i = 0; i < general_labels.size(); ++i) {
    ImGui::Text("%s", general_labels[i].data());
    ImGui::SameLine(120.0f);

    const bool waiting = state.waiting_for_bind == static_cast<int>(i + KCount);
    std::string button_label =
        waiting ? "Press a key..."
                : std::string("Bind##") + std::string(general_labels[i]);
    if (ImGui::Button(button_label.c_str()))
      state.waiting_for_bind = static_cast<int>(i + KCount);

    ImGui::SameLine(240.0f);
    ImGui::Text("%s", SDL_GetKeyName(settings.general_keybinds[i]));
  }

  ImGui::End();
}

void GbcImGui::build_bios_error_popup(UiState &state, const std::string& message) {
  ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 75.0f), ImVec2(500.0f, FLT_MAX));
  ImGui::OpenPopup("BIOS Load Error");
  if (ImGui::BeginPopupModal("BIOS Load Error", &state.show_bios_error,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", message.c_str());
    ImGui::Separator();
    if (ImGui::Button("OK")) {
      state.show_bios_error = false;
    }
    ImGui::EndPopup();
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

std::tuple<ImVec2, ImVec2> GbcImGui::get_min_dialog_size() {
  const float display_w = ImGui::GetIO().DisplaySize.x;
  const float display_h = ImGui::GetIO().DisplaySize.y;
  return std::make_tuple(ImVec2(display_w, display_h),
                         ImVec2(400.0f, 250.0f));
}
