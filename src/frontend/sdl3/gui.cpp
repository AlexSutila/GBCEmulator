#include "frontend/sdl3/gui.hpp"
#include "memory/boot.hpp"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <sys/stat.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

namespace fs = std::filesystem;

namespace {
std::string format_timestamp_local_gui(const std::time_t t) {
  if (t <= 0)
    return "Unknown";
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

std::optional<std::vector<std::uint32_t>>
read_thumb_raw_argb_gui(const std::filesystem::path &path, const int w,
                        const int h) {
  if (w <= 0 || h <= 0)
    return std::nullopt;
  const std::size_t pixel_count =
      static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
  const std::size_t byte_count = pixel_count * sizeof(std::uint32_t);
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    return std::nullopt;
  if (const auto size = static_cast<std::size_t>(f.tellg()); size != byte_count)
    return std::nullopt;
  std::vector<std::uint32_t> out(pixel_count);
  f.seekg(0, std::ios::beg);
  if (!f.read(reinterpret_cast<char *>(out.data()),
              static_cast<std::streamsize>(byte_count)))
    return std::nullopt;
  return out;
}

constexpr std::array<const char *, 5> kCheatFormatLabels{
    "Auto detect", "GameShark/Xploder",
    "Game Genie", "Raw (addr:value, addr?cmp:value)",
    "CodeBreaker"};

std::string cheat_display_name(const Settings::CheatEntry &entry,
                               const std::size_t index) {
  if (!entry.name.empty())
    return entry.name;
  if (!entry.code.empty())
    return entry.code;
  return "Cheat " + std::to_string(index + 1);
}

using DialogId = GbcImGui::DialogId;

struct DetachedDialogSpec {
  DialogId id;
  const char *window_title;
  bool UiState::*visible_flag;
  int default_width;
  int default_height;
  bool detached;
};

constexpr std::array<DetachedDialogSpec,
                     static_cast<std::size_t>(DialogId::Count)>
    kDetachedDialogSpecs{{
        {DialogId::Settings, "IroGB Settings", &UiState::show_settings, 960,
         720, true},
        {DialogId::Cheats, "IroGB Cheats", &UiState::show_cheats, 1120, 760,
         true},
        {DialogId::Keybinds, "IroGB Keybinds", &UiState::show_keybinds, 760,
         700, true},
        {DialogId::Savestates, "IroGB Save States",
         &UiState::show_savestate_manager, 920, 560, true},
        {DialogId::DebugMain, "IroGB Debugger",
         &UiState::show_main_debug_viewer, 1000, 540, true},
        {DialogId::Breakpoints, "IroGB Breakpoints",
         &UiState::show_breakpoints, 760, 520, true},
        {DialogId::MemoryViewer, "IroGB Memory Viewer",
         &UiState::show_memory_viewer, 920, 520, true},
        {DialogId::PpuViewer, "IroGB PPU Viewer", &UiState::show_ppu_viewer,
         700, 520, true},
    }};

[[nodiscard]] constexpr const DetachedDialogSpec &
dialog_spec(const DialogId id) {
  return kDetachedDialogSpecs[static_cast<std::size_t>(id)];
}

[[nodiscard]] bool window_is_hidden(SDL_Window *window) {
  return !window || (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) != 0;
}
} // namespace

void GbcImGui::activate_context(const ImGuiContextState &ctx) {
  if (!ctx.context)
    return;
  ImGui::SetCurrentContext(ctx.context);
  dpi_scale = ctx.dpi_scale;
  active_renderer_ = ctx.renderer;
}

void GbcImGui::init_context(ImGuiContextState &ctx, SDL_Window *window,
                            SDL_Renderer *renderer, const bool owns_window) {
  ctx.window = window;
  ctx.renderer = renderer;
  ctx.owns_window = owns_window;
  ctx.context = ImGui::CreateContext();
  activate_context(ctx);
  ImGui::StyleColorsDark();

  if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
    ImGui::DestroyContext(ctx.context);
    ctx = {};
    throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
  }
  if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx.context);
    ctx = {};
    throw std::runtime_error("Failed to initialize ImGui SDL renderer backend");
  }

  ctx.dpi_scale = 1.0f;
  update_dpi_scale(ctx, SDL_GetWindowDisplayScale(window));

  const ImGuiIO &io = ImGui::GetIO();
  if (fs::exists(font))
    io.Fonts->AddFontFromFileTTF(font.c_str(), base_font_size);
}

void GbcImGui::shutdown_context(ImGuiContextState &ctx) {
  if (!ctx.context)
    return;

  activate_context(ctx);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(ctx.context);

  ctx.context = nullptr;
  if (ctx.owns_window) {
    if (ctx.renderer)
      SDL_DestroyRenderer(ctx.renderer);
    if (ctx.window)
      SDL_DestroyWindow(ctx.window);
  }
  ctx.window = nullptr;
  ctx.renderer = nullptr;
  ctx.dpi_scale = 1.0f;
  ctx.owns_window = false;
}

void GbcImGui::init(const SDLHost &host) {
  settings = Settings::load();

  IMGUI_CHECKVERSION();
  init_context(main_context_, host.get_window(), host.get_renderer(), false);
  use_main_context();

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

void GbcImGui::shutdown() {
  settings.save();
  for (auto &ctx : detached_dialogs_) {
    shutdown_context(ctx);
  }
  shutdown_context(main_context_);
  active_renderer_ = nullptr;
}

void GbcImGui::use_main_context() { activate_context(main_context_); }

void GbcImGui::prepare_dialog_windows(const UiState &state) {
  sync_detached_dialogs(state);
  use_main_context();
}

bool GbcImGui::dialog_is_detached(const DialogId id) {
  return dialog_spec(id).detached;
}

bool GbcImGui::has_detached_dialog_context(const DialogId id) const {
  return detached_dialogs_[dialog_index(id)].context != nullptr;
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

void GbcImGui::ensure_detached_dialog_context(const DialogId id) {
  if (!dialog_is_detached(id))
    return;

  auto &ctx = detached_dialogs_[dialog_index(id)];
  if (ctx.context)
    return;

  const auto &spec = dialog_spec(id);
  SDL_Window *window =
      SDL_CreateWindow(spec.window_title, spec.default_width,
                       spec.default_height,
                       SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE |
                           SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window)
    return;

  SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer) {
    SDL_DestroyWindow(window);
    return;
  }

  SDL_SetRenderVSync(renderer, 0);
  try {
    init_context(ctx, window, renderer, true);
  } catch (...) {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    ctx = {};
  }
}

void GbcImGui::hide_detached_dialog(const DialogId id) const {
  auto &ctx = detached_dialogs_[dialog_index(id)];
  if (ctx.window && !window_is_hidden(ctx.window)) {
    SDL_HideWindow(ctx.window);
  }
}

void GbcImGui::sync_detached_dialogs(const UiState &state) {
  for (const auto &spec : kDetachedDialogSpecs) {
    if (!spec.detached)
      continue;

    if (dialog_visible(spec.id, state)) {
      ensure_detached_dialog_context(spec.id);
      auto &ctx = detached_dialogs_[dialog_index(spec.id)];
      if (ctx.window && window_is_hidden(ctx.window)) {
        SDL_ShowWindow(ctx.window);
        SDL_RaiseWindow(ctx.window);
      }
    } else {
      hide_detached_dialog(spec.id);
    }
  }
}

void GbcImGui::close_detached_dialog(const DialogId id, UiState &state) {
  state.*(dialog_spec(id).visible_flag) = false;
}

bool GbcImGui::dialog_visible(const DialogId id, const UiState &state) {
  return state.*(dialog_spec(id).visible_flag);
}

GbcImGui::ImGuiContextState *
GbcImGui::find_context_for_window(const Uint32 window_id) {
  if (main_context_.window &&
      SDL_GetWindowID(main_context_.window) == window_id) {
    return &main_context_;
  }

  for (auto &ctx : detached_dialogs_) {
    if (ctx.window && SDL_GetWindowID(ctx.window) == window_id) {
      return &ctx;
    }
  }
  return nullptr;
}

const GbcImGui::ImGuiContextState *
GbcImGui::find_context_for_window(const Uint32 window_id) const {
  if (main_context_.window &&
      SDL_GetWindowID(main_context_.window) == window_id) {
    return &main_context_;
  }

  for (const auto &ctx : detached_dialogs_) {
    if (ctx.window && SDL_GetWindowID(ctx.window) == window_id) {
      return &ctx;
    }
  }
  return nullptr;
}

bool GbcImGui::use_detached_dialog_context(const DialogId id, UiState &state) {
  if (!dialog_is_detached(id) || !dialog_visible(id, state))
    return false;

  ensure_detached_dialog_context(id);
  auto &ctx = detached_dialogs_[dialog_index(id)];
  if (!ctx.context)
    return false;

  if (ctx.window && window_is_hidden(ctx.window)) {
    SDL_ShowWindow(ctx.window);
    SDL_RaiseWindow(ctx.window);
  }

  activate_context(ctx);
  return true;
}

void GbcImGui::present_detached_dialog(const DialogId id) const {
  const auto &ctx = detached_dialogs_[dialog_index(id)];
  if (!ctx.context || !ctx.renderer || window_is_hidden(ctx.window))
    return;

  ImGui::SetCurrentContext(ctx.context);
  SDL_SetRenderDrawColor(ctx.renderer, 18, 18, 18, 255);
  SDL_RenderClear(ctx.renderer);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ctx.renderer);
  SDL_RenderPresent(ctx.renderer);
}

void GbcImGui::render(UiState &state, SDLHost &host) {
  // Sync all logs generated since last cycle.
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
  if (state.show_settings &&
      (!dialog_is_detached(DialogId::Settings) ||
       !has_detached_dialog_context(DialogId::Settings))) {
    render_dialog(DialogId::Settings, state, host);
  }
  if (state.show_cheats &&
      (!dialog_is_detached(DialogId::Cheats) ||
       !has_detached_dialog_context(DialogId::Cheats))) {
    render_dialog(DialogId::Cheats, state, host);
  }
  if (state.show_keybinds &&
      (!dialog_is_detached(DialogId::Keybinds) ||
       !has_detached_dialog_context(DialogId::Keybinds))) {
    render_dialog(DialogId::Keybinds, state, host);
  }
  if (state.show_notifications)
    build_notification_window(state);
  if (state.show_about)
    build_about_window(state);
  if (state.show_cart_info)
    build_cart_info_window(state);
}

// Returns true if the event was handled by the GUI and should be ignored by the
// game
bool GbcImGui::process_event(const SDL_Event &e, UiState &ui_state) {
  if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
    for (std::size_t i = 0; i < detached_dialogs_.size(); ++i) {
      auto &ctx = detached_dialogs_[i];
      if (!ctx.window || e.window.windowID != SDL_GetWindowID(ctx.window))
        continue;

      const auto id = static_cast<DialogId>(i);
      close_detached_dialog(id, ui_state);
      hide_detached_dialog(id);
      use_main_context();
      return true;
    }
  }

  use_main_context();
  ImGui_ImplSDL3_ProcessEvent(&e);
  for (auto &ctx : detached_dialogs_) {
    if (!ctx.context)
      continue;
    activate_context(ctx);
    ImGui_ImplSDL3_ProcessEvent(&e);
  }

  // Handle DPI changes
  if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
    ImGuiContextState *ctx = find_context_for_window(e.window.windowID);

    // The event data contains the new scale, but it's safer to query the window
    // because SDL validates it against the specific display
    SDL_Window *window = nullptr;
    if (ctx)
      window = SDL_GetWindowFromID(e.window.windowID);
    if (ctx && window) {
      activate_context(*ctx);
      if (const float new_scale = SDL_GetWindowDisplayScale(window);
          std::abs(new_scale - ctx->dpi_scale) > 0.001f) {
        // We also want to resize the window to maintain physical size
        if (const Uint32 flags = SDL_GetWindowFlags(window);
            !(flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN))) {
          int w, h;
          SDL_GetWindowSize(window, &w, &h);

          // If moving 2.0x -> 1.0x, ratio is 0.5.
          // Window should shrink by half to look the same physical size.
          const float ratio = new_scale / ctx->dpi_scale;
          SDL_SetWindowSize(window,
                            static_cast<int>(static_cast<float>(w) * ratio),
                            static_cast<int>(static_cast<float>(h) * ratio));
        }
        update_dpi_scale(*ctx, new_scale);
      }
    }
    use_main_context();
    return false; // Pass this event to the game (SDLHost might need it too)
  }

  if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
    // Handle key rebinding (highest priority - consumes input)
    if (ui_state.waiting_for_bind && e.type == SDL_EVENT_KEY_DOWN) {
      if (e.key.key != SDLK_ESCAPE) {
        const std::size_t bind_idx = *ui_state.waiting_for_bind;
        if (bind_idx < settings.keybinds.size()) {
          settings.keybinds[bind_idx] = e.key.key;
          settings.keybind_preset_index = kCustomPresetIndex;
        } else {
          const std::size_t general_idx = bind_idx - settings.keybinds.size();
          if (general_idx < settings.general_keybinds.size())
            settings.general_keybinds[general_idx] = e.key.key;
        }
      }
      ui_state.waiting_for_bind.reset();
      use_main_context();
      return true; // CONSUMED: this keypress won't press a button in-game
    }
    // Handle ImGui capture (e.g. typing in a file dialog)
    bool wants_keyboard = false;
    activate_context(main_context_);
    wants_keyboard = ImGui::GetIO().WantCaptureKeyboard;
    if (!wants_keyboard) {
      for (auto &ctx : detached_dialogs_) {
        if (!ctx.context || window_is_hidden(ctx.window))
          continue;
        activate_context(ctx);
        if (ImGui::GetIO().WantCaptureKeyboard) {
          wants_keyboard = true;
          break;
        }
      }
    }
    use_main_context();
    if (wants_keyboard) {
      return true; // CONSUMED
    }
  }
  use_main_context();
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

void GbcImGui::push_transient_status(UiState &state, const LogLevel level,
                                     const std::string &type,
                                     const std::string &summary,
                                     const std::string &details,
                                     const Uint64 duration_ms) {
  state.transient_status_level = level;
  state.transient_status_text =
      type.empty() ? summary : ("[" + type + "] " + summary);
  state.transient_status_details =
      details.empty() ? state.transient_status_text : details;
  state.transient_status_until_ticks = SDL_GetTicks() + duration_ms;
}

void GbcImGui::update_rom_path(const std::string &rom_path) {
  const auto looks_like_url = [](const std::string &s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
  };

  if (!looks_like_url(rom_path)) {
    std::error_code ec;
    const fs::path p{rom_path};
    if (!p.empty() && fs::exists(p, ec)) {
      const auto new_rom_path = p.parent_path().string();
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

/* ImGui windows */
void GbcImGui::build_main_menu_bar(UiState &state) const {
  state.menu_bar_height = ImGui::GetFrameHeight();
  if (ImGui::BeginMainMenuBar()) {
    state.menu_bar_height = ImGui::GetWindowHeight();
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Load ROM..."))
        ImGuiFileDialog::Instance()->OpenDialog(
            "RomFileDialog", "Choose a ROM file", rom_filters.data(),
            rom_sel_conf);
      if (ImGui::MenuItem("Load from URL...")) {
        state.show_load_url_popup = true;
      }
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
              state.load_rom_name = "";
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
      const bool bios_loaded = !settings.prev_bios_path.empty();
      const std::string bios_name =
          bios_loaded ? fs::path(settings.prev_bios_path).filename().string()
                      : "None";
      if (ImGui::BeginMenu("BIOS")) {
        ImGui::TextDisabled("Current: %s", bios_name.c_str());
        if (bios_loaded && ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", settings.prev_bios_path.c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Select BIOS..."))
          ImGuiFileDialog::Instance()->OpenDialog(
              "BiosFileDialog", "Choose a BIN file", bios_filters.data(),
              bios_sel_conf);
        if (!bios_loaded)
          ImGui::BeginDisabled();
        if (ImGui::MenuItem("Unload BIOS"))
          state.request_unload_bios = true;
        if (!bios_loaded)
          ImGui::EndDisabled();
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Quit"))
        state.request_quit = true;
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Options")) {
      if (ImGui::MenuItem("Settings"))
        state.show_settings = true;
      if (ImGui::MenuItem("Cheats"))
        state.show_cheats = true;
      if (ImGui::MenuItem("Keybinds"))
        state.show_keybinds = true;
      if (ImGui::MenuItem("Save States"))
        state.show_savestate_manager = true;
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
      if (ImGui::MenuItem("Open Debugger"))
        state.show_main_debug_viewer = true;
      if (ImGui::MenuItem("Edit Breakpoints"))
        state.show_breakpoints = true;
      if (ImGui::MenuItem("Show Memory Viewer"))
        state.show_memory_viewer = true;
      if (ImGui::MenuItem("Show PPU Viewer"))
        state.show_ppu_viewer = true;
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("About")) {
      if (ImGui::MenuItem("About"))
        state.show_about = true;
      if (ImGui::MenuItem("Cartridge Info"))
        state.show_cart_info = true;
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }
}

void GbcImGui::build_status_bar(UiState &state) const {
  const float height = ImGui::GetFrameHeight();
  state.status_bar_height = height;
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const Uint64 now_ticks = SDL_GetTicks();
  if (!state.transient_status_text.empty() &&
      now_ticks >= state.transient_status_until_ticks) {
    state.transient_status_text.clear();
    state.transient_status_details.clear();
  }

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
        bios_loaded ? fs::path(settings.prev_bios_path).filename().string()
                    : "None";
    ImGui::SameLine();
    ImGui::TextDisabled("| BIOS: %s", bios_name.c_str());
    if (bios_loaded && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", settings.prev_bios_path.c_str());
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
      state.load_rom_name = "";
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

void GbcImGui::build_rom_source_window(UiState &state) const {
  // --- Load from URL popup ---
  if (state.show_load_url_popup) {
    ImGui::OpenPopup("Load ROM/ZIP from URL");
    state.show_load_url_popup = false;
  }

  if (ImGui::BeginPopupModal("Load ROM/ZIP from URL", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(
        "Enter a link to a .gb/.gbc ROM or a .zip archive");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(520.0f * dpi_scale);
    ImGui::InputTextWithHint("##rom_url", "https://example.com/game.zip",
                             state.load_url_input, IM_ARRAYSIZE(state.load_url_input));

    const bool can_load = state.load_url_input[0] != '\0';
    if (!can_load) ImGui::BeginDisabled();

    if (ImGui::Button("Load")) {
      state.load_rom_path = state.load_url_input;
      state.load_rom_name = "";
      state.request_load_rom = true;
      ImGui::CloseCurrentPopup();
    }

    if (!can_load) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // --- ZIP ROM chooser popup ---
  if (state.show_zip_picker_popup) {
    ImGui::OpenPopup("Choose ROM from ZIP");
  }

  if (ImGui::BeginPopupModal("Choose ROM from ZIP", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!state.zip_picker_title.empty()) {
      ImGui::TextUnformatted(state.zip_picker_title.c_str());
      ImGui::Spacing();
    } else {
      ImGui::TextUnformatted("Multiple ROM files were found in this ZIP");
      ImGui::Spacing();
    }

    const float lb_w = 520.0f * dpi_scale;
    const float lb_h = 220.0f * dpi_scale;
    if (ImGui::BeginListBox("##zip_rom_list", ImVec2(lb_w, lb_h))) {
      for (int i = 0; i < static_cast<int>(state.zip_rom_entries.size()); ++i) {
        const bool selected = (i == state.zip_rom_selected_idx);
        if (ImGui::Selectable(state.zip_rom_entries[i].c_str(), selected)) {
          state.zip_rom_selected_idx = i;
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndListBox();
    }

    const bool has_entries = !state.zip_rom_entries.empty();
    if (!has_entries) ImGui::BeginDisabled();

    if (ImGui::Button("OK")) {
      state.zip_picker_action = 1;
      state.show_zip_picker_popup = false;
      ImGui::CloseCurrentPopup();
    }

    if (!has_entries) ImGui::EndDisabled();

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
  ImGui::Begin("Settings", &state.show_settings);
  ImGui::SeparatorText("General");
  ImGui::Checkbox("Fast forward", &state.fast_forward);
  ImGui::Checkbox("Force DMG monochrome", &settings.force_mono_dmg);
  {
    static std::array<char, 512> save_root_input{};
    static std::string last_save_root;
    if (last_save_root != settings.save_root_dir) {
      snprintf(save_root_input.data(), save_root_input.size(), "%s",
               settings.save_root_dir.c_str());
      last_save_root = settings.save_root_dir;
    }

    ImGui::SetNextItemWidth(320.0f * dpi_scale);
    if (ImGui::InputText("Save dir", save_root_input.data(),
                         save_root_input.size())) {
      settings.save_root_dir = save_root_input.data();
      last_save_root = settings.save_root_dir;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##save_root")) {
      settings.save_root_dir = "./saves";
      snprintf(save_root_input.data(), save_root_input.size(), "%s",
               settings.save_root_dir.c_str());
      last_save_root = settings.save_root_dir;
    }
    ImGui::TextDisabled(
        "Saves: game-name - checksum.sav");
  }
  {
    static std::array<char, 512> savestate_root_input{};
    static std::string last_savestate_root;
    if (last_savestate_root != settings.savestate_root_dir) {
      snprintf(savestate_root_input.data(), savestate_root_input.size(), "%s",
               settings.savestate_root_dir.c_str());
      last_savestate_root = settings.savestate_root_dir;
    }

    ImGui::SetNextItemWidth(320.0f * dpi_scale);
    if (ImGui::InputText("Savestate dir", savestate_root_input.data(),
                         savestate_root_input.size())) {
      settings.savestate_root_dir = savestate_root_input.data();
      last_savestate_root = settings.savestate_root_dir;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##savestate_root")) {
      settings.savestate_root_dir = "./savestates";
      snprintf(savestate_root_input.data(), savestate_root_input.size(), "%s",
               settings.savestate_root_dir.c_str());
      last_savestate_root = settings.savestate_root_dir;
    }
    ImGui::TextDisabled(
        "Savestate folders: game-name - checksum");
  }
  {
    static std::array<char, 512> cheat_root_input{};
    static std::string last_cheat_root;
    if (last_cheat_root != settings.cheat_root_dir) {
      snprintf(cheat_root_input.data(), cheat_root_input.size(), "%s",
               settings.cheat_root_dir.c_str());
      last_cheat_root = settings.cheat_root_dir;
    }

    ImGui::SetNextItemWidth(320.0f * dpi_scale);
    if (ImGui::InputText("Cheat dir", cheat_root_input.data(),
                         cheat_root_input.size())) {
      settings.cheat_root_dir = cheat_root_input.data();
      last_cheat_root = settings.cheat_root_dir;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##cheat_root")) {
      settings.cheat_root_dir = "./cheats";
      snprintf(cheat_root_input.data(), cheat_root_input.size(), "%s",
               settings.cheat_root_dir.c_str());
      last_cheat_root = settings.cheat_root_dir;
    }
    ImGui::TextDisabled("Cheats: game-name - checksum.cht");
  }
  {
    ImGui::SetNextItemWidth(120.0f * dpi_scale);
    if (ImGui::InputInt("Max quicksaves", &settings.max_quicksaves)) {
      if (settings.max_quicksaves < 0)
        settings.max_quicksaves = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##max_quicksaves")) {
      settings.max_quicksaves = Settings::default_max_quicksaves;
    }
    ImGui::TextDisabled("0 = unlimited");
  }
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
  ImGui::End();
}

void GbcImGui::build_cheats_window(UiState &state) {
  ImGui::SetNextWindowSize(ImVec2(860.0f * dpi_scale, 520.0f * dpi_scale),
                           ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Cheats", &state.show_cheats)) {
    ImGui::End();
    return;
  }

  bool settings_dirty = false;
  auto &cheats = settings.cheats;
  if (state.selected_cheat_idx >= static_cast<int>(cheats.size()))
    state.selected_cheat_idx =
        cheats.empty() ? -1 : static_cast<int>(cheats.size()) - 1;

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
      state.selected_cheat_idx >= 0 &&
      state.selected_cheat_idx < static_cast<int>(cheats.size());
  if (!can_edit_selected)
    ImGui::BeginDisabled();
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
  if (!can_edit_selected)
    ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Enable All")) {
    for (auto &entry : cheats)
      entry.enabled = true;
    settings_dirty = !cheats.empty();
  }

  ImGui::SameLine();
  if (ImGui::Button("Disable All")) {
    for (auto &entry : cheats)
      entry.enabled = false;
    settings_dirty = !cheats.empty();
  }

  ImGui::Spacing();

  if (ImGui::BeginTable("cheat_layout", 2,
                        ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Cheat List", ImGuiTableColumnFlags_WidthStretch,
                            0.43f);
    ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch,
                            0.57f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginTable("cheat_entries", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 0.0f))) {
      ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed,
                              40.0f * dpi_scale);
      ImGui::TableSetupColumn("Cheat", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      for (std::size_t i = 0; i < cheats.size(); ++i) {
        auto &entry = cheats[i];
        const bool selected = state.selected_cheat_idx == static_cast<int>(i);

        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox("##enabled", &entry.enabled))
          settings_dirty = true;

        ImGui::TableSetColumnIndex(1);
        if (const auto label = cheat_display_name(entry, i);
            ImGui::Selectable(label.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
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
        state.selected_cheat_idx >= 0 &&
        state.selected_cheat_idx < static_cast<int>(cheats.size());
    if (!has_selection) {
      ImGui::TextDisabled("Select a cheat from the list to edit.\nCheat codes are tied to ROMs.");
    } else {
      auto &entry = cheats[static_cast<std::size_t>(state.selected_cheat_idx)];

      if (ImGui::InputText("Name", &entry.name))
        settings_dirty = true;
      if (ImGui::InputText("Code", &entry.code))
        settings_dirty = true;

      int format =
          std::clamp(entry.format, 0, static_cast<int>(kCheatFormatLabels.size()) - 1);
      if (format != entry.format) {
        entry.format = format;
        settings_dirty = true;
      }
      if (ImGui::Combo("Format", &format, kCheatFormatLabels.data(),
                       kCheatFormatLabels.size())) {
        entry.format = format;
        settings_dirty = true;
      }

      if (ImGui::Checkbox("Enabled##editor", &entry.enabled))
        settings_dirty = true;
      ImGui::Text("Notes");
      if (ImGui::InputTextMultiline("Notes", &entry.notes,
                                    ImVec2(-1.0f, 180.0f * dpi_scale)))
        settings_dirty = true;

      ImGui::TextDisabled(
          "Compare is supported in Game Genie and Raw (AAAA?CC:VV).");
    }

    ImGui::EndTable();
  }

  if (settings_dirty) {
    state.cheats_dirty = true;
    state.cheats_file_dirty = true;
  }
  ImGui::End();
}

void GbcImGui::build_savestate_manager_window(
    UiState &state, SDL_Renderer *renderer, const bool emulator_ready,
    const std::filesystem::path &savestate_dir,
    std::array<char, 96> &manual_label_input,
    std::vector<SavestateEntry> &savestate_entries,
    std::optional<std::filesystem::path> &savestate_selected_path,
    const SavestateManagerCallbacks &callbacks) {
  if (!state.show_savestate_manager)
    return;

  ImGui::SetNextWindowSize(ImVec2(920, 560), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Save States", &state.show_savestate_manager)) {
    ImGui::End();
    return;
  }

  if (savestate_dir.empty()) {
    ImGui::TextDisabled("Load a ROM to manage savestates.");
    ImGui::End();
    return;
  }

  ImGui::Text("Directory: %s", savestate_dir.string().c_str());
  ImGui::InputText("Label", manual_label_input.data(), manual_label_input.size());

  if (!emulator_ready)
    ImGui::BeginDisabled();
  if (ImGui::Button("Save") && callbacks.queue_manual_save) {
    callbacks.queue_manual_save(manual_label_input.data());
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Most Recent") && callbacks.request_load_most_recent) {
    callbacks.request_load_most_recent();
  }
  if (!emulator_ready)
    ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Refresh") && callbacks.refresh) {
    callbacks.refresh();
  }

  ImGui::Separator();

  std::optional<std::filesystem::path> delete_path = std::nullopt;
  std::optional<std::filesystem::path> load_path = std::nullopt;

  if (ImGui::BeginTable("savestate_manager_layout", 2,
                        ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch,
                            0.38f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginTable("savestate_table", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 0.0f))) {
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
      ImGui::TableSetupColumn("Taken", ImGuiTableColumnFlags_WidthFixed,
                              170.0f);
      ImGui::TableSetupColumn("Label");
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableHeadersRow();

      for (std::size_t i = 0; i < savestate_entries.size(); ++i) {
        auto &entry = savestate_entries[i];
        const bool selected = savestate_selected_path.has_value() &&
                              *savestate_selected_path == entry.state_path;

        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Selectable(entry.kind.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
          savestate_selected_path = entry.state_path;
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(
            format_timestamp_local_gui(entry.created_at).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(entry.label.c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", entry.state_path.filename().string().c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%zu KB", static_cast<std::size_t>(
                                  (entry.file_size + 1023u) / 1024u));
        ImGui::PopID();
      }
      ImGui::EndTable();
    }

    ImGui::TableSetColumnIndex(1);
    auto selected_it = savestate_entries.end();
    if (savestate_selected_path.has_value()) {
      selected_it =
          std::ranges::find_if(savestate_entries, [&](const SavestateEntry &e) {
            return e.state_path == *savestate_selected_path;
          });
    }

    if (selected_it == savestate_entries.end()) {
      ImGui::TextDisabled("No savestate selected.");
    } else {
      auto &entry = *selected_it;
      ImGui::Text("Type: %s", entry.kind.c_str());
      ImGui::Text("Taken: %s",
                  format_timestamp_local_gui(entry.created_at).c_str());
      ImGui::Text("File: %s", entry.state_path.filename().string().c_str());
      ImGui::Text("Size: %zu bytes", static_cast<std::size_t>(entry.file_size));
      ImGui::Spacing();

      if (!emulator_ready)
        ImGui::BeginDisabled();
      if (ImGui::Button("Load")) {
        load_path = entry.state_path;
      }
      if (!emulator_ready)
        ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Delete")) {
        delete_path = entry.state_path;
      }

      ImGui::SeparatorText("Thumbnail");
      if (entry.thumb_texture && entry.thumb_renderer != renderer) {
        SDL_DestroyTexture(entry.thumb_texture);
        entry.thumb_texture = nullptr;
        entry.thumb_texture_attempted = false;
        entry.thumb_renderer = nullptr;
      }
      if (renderer && !entry.thumb_texture && !entry.thumb_texture_attempted) {
        entry.thumb_texture_attempted = true;
        if (std::filesystem::exists(entry.thumb_path)) {
          if (const auto pixels = read_thumb_raw_argb_gui(
                  entry.thumb_path, entry.thumb_w, entry.thumb_h);
              pixels.has_value()) {
            entry.thumb_texture = SDL_CreateTexture(
                renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                entry.thumb_w, entry.thumb_h);
            if (entry.thumb_texture) {
              entry.thumb_renderer = renderer;
              SDL_UpdateTexture(entry.thumb_texture, nullptr, pixels->data(),
                                entry.thumb_w *
                                    static_cast<int>(sizeof(std::uint32_t)));
              SDL_SetTextureScaleMode(entry.thumb_texture,
                                      SDL_SCALEMODE_NEAREST);
            }
          }
        }
      }
      if (entry.thumb_texture) {
        constexpr float max_w = 260.0f;
        const float scale_ =
            std::min(max_w / static_cast<float>(entry.thumb_w), 4.0f);
        ImGui::Image(entry.thumb_texture,
                     ImVec2(entry.thumb_w * scale_, entry.thumb_h * scale_));
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
    if (!state.waiting_for_bind.has_value()) {
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

void GbcImGui::update_dpi_scale(ImGuiContextState &ctx, const float new_scale) {
  ImGui::GetStyle() = ImGuiStyle();
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();

  style.FontScaleDpi = new_scale;
  // Calculate relative change (e.g., moving 1.0 -> 2.0 means factor 2.0)
  const float relative_scale = new_scale / ctx.dpi_scale;

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

  ctx.dpi_scale = new_scale;
  dpi_scale = new_scale;
}

void GbcImGui::build_about_window(UiState &state) {
  // Set a default size and position (bottom right)
  ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("About", &state.show_about)) {
    ImGui::SeparatorText("Project");
    ImGui::TextUnformatted("IroGB");
    ImGui::TextLinkOpenURL("https://kaze.moe/TismForge/IroGB",
                           "https://kaze.moe/TismForge/IroGB");

    ImGui::SeparatorText("Authors");
    ImGui::BulletText("Alex Sutila");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("https://github.com/alexsutila",
                           "https://github.com/alexsutila");
    ImGui::BulletText("Xuanli Lin");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("https://github.com/kazum1kun",
                           "https://github.com/kazum1kun");

    ImGui::SeparatorText("Credits");
    ImGui::TextWrapped("We would like to thank the following open source projects for providing tools and resources that were instrumental in the development of IroGB:");
    populate_credits();

    ImGui::SeparatorText("License");
    ImGui::Text("IroGB is licensed under the GPLv3 License. See ");
    ImGui::TextLinkOpenURL("LICENSE", "https://kaze.moe/TismForge/IroGB/raw/branch/release/LICENSE");
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
  case LogLevel::Status:
  case LogLevel::Info:
    return {0.71f, 0.74f, 0.40f, 1.0f}; // Light green #b5bd68
  default:
    return {0.77f, 0.78f, 0.78f, 1.0f}; // Grey #c5c8c6
  }
}

void GbcImGui::populate_credits() {
  for (const auto& [name, url, license] : kThirdPartyProjects) {
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextLinkOpenURL(name.data(), url.data());
    ImGui::SameLine();
    ImGui::Text("%s", license.data());
  }
}
