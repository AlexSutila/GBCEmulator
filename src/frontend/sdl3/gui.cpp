#include "frontend/sdl3/gui.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace {
using DialogId = GbcImGui::DialogId;

struct InlineDialogSpec {
  DialogId id;
  bool UiState::*visible_flag;
};

constexpr std::array<InlineDialogSpec, 3> kInlineDialogs{
    {
     {DialogId::Settings, &UiState::show_settings},
     {DialogId::Cheats, &UiState::show_cheats},
     {DialogId::Keybinds, &UiState::show_keybinds},
     }
};

bool should_render_attached_dialog(const GbcImGui &gui, const UiState &state, const DialogId id,
                                   const bool UiState::*visible_flag) {
  return state.*visible_flag &&
         (!GbcImGui::dialog_is_detached(id) || !gui.has_detached_dialog_context(id));
}
} // namespace

void GbcImGui::init(const SDLHost &host) {
  settings = Settings::load();

  IMGUI_CHECKVERSION();
  init_context(main_context_, host.get_window(), host.get_renderer(), false);
  use_main_context();

  rom_sel_conf.path = settings.rom_dir;
  rom_sel_conf.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ReadOnlyFileNameField;
  bios_sel_conf.path = settings.bios_dir;
  bios_sel_conf.flags = rom_sel_conf.flags;

  if (settings.keybind_preset_index != kCustomPresetIndex) {
    apply_keybind_preset(settings.keybinds, settings.keybind_preset_index);
  }
}

void GbcImGui::shutdown() {
  settings.save();
  for (auto &ctx : detached_dialogs_) {
    shutdown_context(ctx);
  }
  shutdown_context(main_context_);
  active_renderer_ = nullptr;
}

void GbcImGui::render_dialog(const DialogId id, UiState &state, SDLHost &host) {
  switch (id) {
  case DialogId::Settings:
    build_settings_window(state, host);
    break;
  case DialogId::Cheats:
    build_cheats_window(state);
    break;
  case DialogId::Keybinds:
    build_keybinds_window(state);
    break;
  default:
    break;
  }
}

void GbcImGui::render(UiState &state, SDLHost &host) {
  for (const auto new_logs = Logger::consume();
       const auto &[level, type, summary, message, timestamp] : new_logs) {
    if (level == LogLevel::Status) {
      push_transient_status(state, level, type, summary, message);
    } else {
      push_notification(state, level, type, summary, message, timestamp);
    }
  }

  build_main_menu_bar(state);
  build_status_bar(state);
  build_file_dialogs(state);
  build_rom_source_window(state);

  for (const auto &spec : kInlineDialogs) {
    if (should_render_attached_dialog(*this, state, spec.id, spec.visible_flag)) {
      render_dialog(spec.id, state, host);
    }
  }

  if (state.show_notifications) {
    build_notification_window(state);
  }
  if (state.show_about) {
    build_about_window(state);
  }
  if (state.show_cart_info) {
    build_cart_info_window(state);
  }
}

bool GbcImGui::process_event(const SDL_Event &event, UiState &ui_state) {
  if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
    for (std::size_t i = 0; i < detached_dialogs_.size(); ++i) {
      auto &ctx = detached_dialogs_[i];
      if (!ctx.window || event.window.windowID != SDL_GetWindowID(ctx.window)) {
        continue;
      }

      const auto id = static_cast<DialogId>(i);
      close_detached_dialog(id, ui_state);
      hide_detached_dialog(id);
      use_main_context();
      return true;
    }
  }

  // Every detached dialog owns an independent ImGui backend state, so SDL input
  // has to be fanned out to every live context before we decide whether the
  // emulator itself should see the event
  use_main_context();
  ImGui_ImplSDL3_ProcessEvent(&event);
  for (auto &ctx : detached_dialogs_) {
    if (!ctx.context) {
      continue;
    }
    activate_context(ctx);
    ImGui_ImplSDL3_ProcessEvent(&event);
  }

  if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
    ImGuiContextState *ctx = find_context_for_window(event.window.windowID);
    SDL_Window *window = ctx ? SDL_GetWindowFromID(event.window.windowID) : nullptr;
    if (ctx && window) {
      activate_context(*ctx);
      if (const float new_scale = SDL_GetWindowDisplayScale(window);
          std::abs(new_scale - ctx->dpi_scale) > 0.001f) {
        // SDL reports DPI changes per window. We rescale the backing ImGui
        // context and then resize the SDL window so detached dialogs keep the
        // same physical footprint when crossing displays
        const Uint32 flags = SDL_GetWindowFlags(window);
        if (!(flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN))) {
          int width = 0;
          int height = 0;
          SDL_GetWindowSize(window, &width, &height);
          const float ratio = new_scale / ctx->dpi_scale;
          SDL_SetWindowSize(window, width * static_cast<int>(ratio),
                            height * static_cast<int>(ratio));
        }
        update_dpi_scale(*ctx, new_scale);
      }
    }
    use_main_context();
    return false;
  }

  if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
    if (ui_state.waiting_for_bind && event.type == SDL_EVENT_KEY_DOWN) {
      if (event.key.key != SDLK_ESCAPE) {
        const std::size_t bind_idx = *ui_state.waiting_for_bind;
        if (bind_idx < settings.keybinds.size()) {
          settings.keybinds[bind_idx] = event.key.key;
          settings.keybind_preset_index = kCustomPresetIndex;
        } else {
          const std::size_t general_idx = bind_idx - settings.keybinds.size();
          if (general_idx < settings.general_keybinds.size()) {
            settings.general_keybinds[general_idx] = event.key.key;
          }
        }
      }
      ui_state.waiting_for_bind.reset();
      use_main_context();
      return true;
    }

    const auto context_wants_keyboard = [this](const ImGuiContextState &ctx) {
      if (!ctx.context || window_is_hidden(ctx.window)) {
        return false;
      }
      activate_context(ctx);
      return ImGui::GetIO().WantCaptureKeyboard;
    };

    bool wants_keyboard = false;
    activate_context(main_context_);
    wants_keyboard = ImGui::GetIO().WantCaptureKeyboard;
    if (!wants_keyboard) {
      for (auto &ctx : detached_dialogs_) {
        if (context_wants_keyboard(ctx)) {
          wants_keyboard = true;
          break;
        }
      }
    }

    use_main_context();
    if (wants_keyboard) {
      return true;
    }
  }

  use_main_context();
  return false;
}

void GbcImGui::push_notification(UiState &state, const LogLevel level, const std::string &type,
                                 const std::string &summary, const std::string &details,
                                 const time_t timestamp) {
  Notification notif;
  notif.id = state.next_notify_id++;
  notif.level = level;
  notif.type = type;
  notif.summary = summary;
  notif.details = details.empty() ? summary : details;
  notif.timestamp = timestamp;

  state.notifications.push_back(notif);
}

void GbcImGui::push_transient_status(UiState &state, const LogLevel level, const std::string &type,
                                     const std::string &summary, const std::string &details,
                                     const Uint64 duration_ms) {
  state.transient_status_level = level;
  state.transient_status_text = type.empty() ? summary : ("[" + type + "] " + summary);
  state.transient_status_details = details.empty() ? state.transient_status_text : details;
  state.transient_status_until_ticks = SDL_GetTicks() + duration_ms;
}

void GbcImGui::update_rom_path(const std::string &rom_path) {
  const auto looks_like_url = [](const std::string &path) {
    return path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0;
  };

  if (!looks_like_url(rom_path)) {
    std::error_code ec;
    const fs::path path{rom_path};
    if (!path.empty() && fs::exists(path, ec)) {
      const auto new_rom_path = path.parent_path().string();
      rom_sel_conf.path = new_rom_path;
      settings.rom_dir = new_rom_path;
    }
  }

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

void GbcImGui::clear_bios_path() {
  settings.prev_bios_path.clear();
  settings.save();
}
