#include "frontend/sdl3/gui.hpp"
#include <imgui_internal.h>
#include <stdexcept>
#include <utility>

namespace {
using DialogId = GbcImGui::DialogId;

struct DetachedDialogSpec {
  DialogId id;
  const char *window_title;
  bool UiState::*visible_flag;
  int default_width;
  int default_height;
  bool detached;
};

constexpr std::array<DetachedDialogSpec, static_cast<std::size_t>(DialogId::Count)>
    kDetachedDialogSpecs{
        {
         {DialogId::Settings, "Settings", &UiState::show_settings, 800, 550, true},
         {DialogId::Cheats, "Cheats", &UiState::show_cheats, 850, 600, true},
         {DialogId::Keybinds, "Keybinds", &UiState::show_keybinds, 480, 500, true},
         {DialogId::Savestates, "Save States", &UiState::show_savestate_manager, 990, 615, true},
         {DialogId::DebugMain, "Debugger", &UiState::show_main_debug_viewer, 790, 460, true},
         {DialogId::Breakpoints, "Breakpoints", &UiState::show_breakpoints, 360, 400, true},
         {DialogId::MemoryViewer, "Memory Viewer", &UiState::show_memory_viewer, 840, 660, true},
         {DialogId::PpuViewer, "PPU Viewer", &UiState::show_ppu_viewer, 425, 570, true},
         }
};

[[nodiscard]] constexpr const DetachedDialogSpec &dialog_spec(const DialogId id) {
  return kDetachedDialogSpecs[static_cast<std::size_t>(id)];
}

SDL_HitTestResult SDLCALL detached_dialog_hit_test(SDL_Window *window, const SDL_Point *area,
                                                   void * /*data*/) {
  if (!window || !area) {
    return SDL_HITTEST_NORMAL;
  }

  int width = 0;
  int height = 0;
  SDL_GetWindowSize(window, &width, &height);
  if (width <= 0 || height <= 0) {
    return SDL_HITTEST_NORMAL;
  }

  const float scale = SDL_GetWindowDisplayScale(window);
  const int resize_border = std::max(6, static_cast<int>(std::lround(6.0f * scale)));
  const int title_bar_height = std::max(28, static_cast<int>(std::lround(28.0f * scale)));
  const int close_button_width = std::max(48, static_cast<int>(std::lround(48.0f * scale)));

  const bool left = area->x < resize_border;
  const bool right = area->x >= width - resize_border;
  const bool top = area->y < resize_border;
  const bool bottom = area->y >= height - resize_border;

  if (top && left) {
    return SDL_HITTEST_RESIZE_TOPLEFT;
  }
  if (top && right) {
    return SDL_HITTEST_RESIZE_TOPRIGHT;
  }
  if (bottom && left) {
    return SDL_HITTEST_RESIZE_BOTTOMLEFT;
  }
  if (bottom && right) {
    return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
  }
  if (top) {
    return SDL_HITTEST_RESIZE_TOP;
  }
  if (bottom) {
    return SDL_HITTEST_RESIZE_BOTTOM;
  }
  if (left) {
    return SDL_HITTEST_RESIZE_LEFT;
  }
  if (right) {
    return SDL_HITTEST_RESIZE_RIGHT;
  }

  const bool in_drag_strip = area->y < title_bar_height;
  const bool over_close_button = area->x >= width - close_button_width;
  return (in_drag_strip && !over_close_button) ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}
} // namespace

void GbcImGui::activate_context(const ImGuiContextState &ctx) {
  if (!ctx.context) {
    return;
  }
  ImGui::SetCurrentContext(ctx.context);
  dpi_scale = ctx.dpi_scale;
  active_renderer_ = ctx.renderer;
}

void GbcImGui::init_context(ImGuiContextState &ctx, SDL_Window *window, SDL_Renderer *renderer,
                            const bool owns_window) {
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
  if (std::filesystem::exists(font)) {
    io.Fonts->AddFontFromFileTTF(font.c_str(), base_font_size);
  }
}

void GbcImGui::shutdown_context(ImGuiContextState &ctx) {
  if (!ctx.context) {
    return;
  }

  activate_context(ctx);
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(ctx.context);

  ctx.context = nullptr;
  if (ctx.owns_window) {
    if (ctx.renderer) {
      SDL_DestroyRenderer(ctx.renderer);
    }
    if (ctx.window) {
      SDL_DestroyWindow(ctx.window);
    }
  }
  ctx.window = nullptr;
  ctx.renderer = nullptr;
  ctx.dpi_scale = 1.0f;
  ctx.owns_window = false;
}

void GbcImGui::use_main_context() { activate_context(main_context_); }

void GbcImGui::prepare_dialog_windows(const UiState &state) {
  sync_detached_dialogs(state);
  use_main_context();
}

bool GbcImGui::dialog_is_detached(const DialogId id) { return dialog_spec(id).detached; }

bool GbcImGui::has_detached_dialog_context(const DialogId id) const {
  return detached_dialogs_[dialog_index(id)].context != nullptr;
}

void GbcImGui::ensure_detached_dialog_context(const DialogId id) {
  if (!dialog_is_detached(id)) {
    return;
  }

  auto &ctx = detached_dialogs_[dialog_index(id)];
  if (ctx.context) {
    return;
  }

  const auto &spec = dialog_spec(id);
  SDL_Window *window = SDL_CreateWindow(spec.window_title, spec.default_width, spec.default_height,
                                        kDetachedDialogWindowFlags);
  if (!window) {
    return;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer) {
    SDL_DestroyWindow(window);
    return;
  }

  SDL_SetWindowHitTest(window, detached_dialog_hit_test, nullptr);
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
    if (!spec.detached) {
      continue;
    }

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

bool GbcImGui::rendering_detached_dialog(const DialogId id) const {
  const auto &ctx = detached_dialogs_[dialog_index(id)];
  return ctx.context && ImGui::GetCurrentContext() == ctx.context;
}

GbcImGui::ImGuiContextState *GbcImGui::find_context_for_window(const Uint32 window_id) {
  return const_cast<ImGuiContextState *>(std::as_const(*this).find_context_for_window(window_id));
}

const GbcImGui::ImGuiContextState *GbcImGui::find_context_for_window(const Uint32 window_id) const {
  if (main_context_.window && SDL_GetWindowID(main_context_.window) == window_id) {
    return &main_context_;
  }

  for (const auto &ctx : detached_dialogs_) {
    if (ctx.window && SDL_GetWindowID(ctx.window) == window_id) {
      return &ctx;
    }
  }
  return nullptr;
}

bool GbcImGui::use_detached_dialog_context(const DialogId id, const UiState &state) {
  if (!dialog_is_detached(id) || !dialog_visible(id, state)) {
    return false;
  }

  ensure_detached_dialog_context(id);
  auto &ctx = detached_dialogs_[dialog_index(id)];
  if (!ctx.context) {
    return false;
  }

  if (ctx.window && window_is_hidden(ctx.window)) {
    SDL_ShowWindow(ctx.window);
    SDL_RaiseWindow(ctx.window);
  }

  activate_context(ctx);
  return true;
}

void GbcImGui::present_detached_dialog(const DialogId id) const {
  const auto &ctx = detached_dialogs_[dialog_index(id)];
  if (!ctx.context || !ctx.renderer || window_is_hidden(ctx.window)) {
    return;
  }

  ImGui::SetCurrentContext(ctx.context);
  SDL_SetRenderDrawColor(ctx.renderer, 18, 18, 18, 255);
  SDL_RenderClear(ctx.renderer);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ctx.renderer);
  SDL_RenderPresent(ctx.renderer);
}

void GbcImGui::update_dpi_scale(ImGuiContextState &ctx, const float new_scale) {
  ImGui::GetStyle() = ImGuiStyle();
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();

  style.FontScaleDpi = new_scale;
  const float relative_scale = new_scale / ctx.dpi_scale;
  style.ScaleAllSizes(relative_scale);

  if (ImGuiContext *imgui_ctx = ImGui::GetCurrentContext()) {
    // Each detached window owns its own ImGui context. When DPI changes we
    // rescale existing window geometry so dialogs do not jump back to their
    // pre-scale logical size the next frame
    for (int i = 0; i < imgui_ctx->Windows.Size; ++i) {
      ImGuiWindow *window = imgui_ctx->Windows[i];
      window->Pos.x *= relative_scale;
      window->Pos.y *= relative_scale;
      window->Size.x *= relative_scale;
      window->Size.y *= relative_scale;
      window->SizeFull.x *= relative_scale;
      window->SizeFull.y *= relative_scale;
    }
  }

  ctx.dpi_scale = new_scale;
  dpi_scale = new_scale;
}
