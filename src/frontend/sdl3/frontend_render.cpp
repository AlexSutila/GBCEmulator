#include "frontend/sdl3/frontend.hpp"

namespace {
struct DebugDialogSpec {
  GbcImGui::DialogId id;
  bool UiState::*visible_flag;
};

constexpr std::array<DebugDialogSpec, 4> kDebuggerDialogs{
    {
     {GbcImGui::DialogId::DebugMain, &UiState::show_main_debug_viewer},
     {GbcImGui::DialogId::Breakpoints, &UiState::show_breakpoints},
     {GbcImGui::DialogId::MemoryViewer, &UiState::show_memory_viewer},
     {GbcImGui::DialogId::PpuViewer, &UiState::show_ppu_viewer},
     }
};

bool should_render_attached_dialog(const GbcImGui &gui, const GbcImGui::DialogId id,
                                   const bool visible) {
  return visible && (!GbcImGui::dialog_is_detached(id) || !gui.has_detached_dialog_context(id));
}
} // namespace

const std::uint32_t *SDL3Frontend::get_front_buffer() const {
  return framebuffers[front_index.load(std::memory_order_acquire)].get();
}

void SDL3Frontend::process_events() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) {
      running = false;
      continue;
    }

    bool consumed = false;
    if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
      // `gui.process_event()` can resize windows on DPI changes, which
      // synchronously generates more window events and can deadlock if we hold
      // `ui_mutex` across that SDL callback chain.
      consumed = gui.process_event(event, ui_state);
    } else {
      std::lock_guard lock(ui_mutex);
      consumed = gui.process_event(event, ui_state);
    }
    if (consumed) {
      continue;
    }

    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
      handle_keypress(event.key.key, event.type == SDL_EVENT_KEY_DOWN);
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
               event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
      handle_controller_press(static_cast<SDL_GamepadButton>(event.gbutton.button),
                              event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
    }

    if (event.type == SDL_EVENT_DROP_FILE || event.type == SDL_EVENT_DROP_TEXT) {
      handle_drop(event);
    }
  }
}

void SDL3Frontend::render_frame() {
  // `render_frame()` can be entered from the main loop and from SDL's event
  // watcher during resize/move storms. The atomic flag keeps those paths from
  // re-entering the renderer concurrently.
  if (render_guard.test_and_set(std::memory_order_acquire)) {
    return;
  }
  struct Guard {
    std::atomic_flag &flag;
    ~Guard() { flag.clear(std::memory_order_release); }
  } guard{render_guard};

  const auto now_ns = steady_now_ns();
  const auto until_ns = suppress_vsync_until_ns.load(std::memory_order_relaxed);
  host.set_vsync(now_ns >= until_ns);

  const bool force_mono = gui.get_settings_c().force_mono_dmg;
  const bool cgb_mode = is_cgb.load(std::memory_order_relaxed);
  if (force_mono != last_force_mono_dmg || cgb_mode != last_cgb_mode) {
    last_force_mono_dmg = force_mono;
    last_cgb_mode = cgb_mode;
    video_dirty.store(true, std::memory_order_relaxed);
  }

  if (video_dirty.exchange(false, std::memory_order_acq_rel)) {
    host.update_texture(get_front_buffer(), framebuf_width, framebuf_height, cgb_mode, force_mono);
  }

  const auto now = Clock::now();
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_check).count();
  if (elapsed_ms >= 500) {
    const uint64_t current_count = emulated_frame_count.load(std::memory_order_relaxed);
    const uint64_t frames = current_count - last_frame_count;
    ui_state.current_fps = static_cast<double>(frames) * 1000.0 / static_cast<double>(elapsed_ms);
    last_frame_count = current_count;
    last_fps_check = now;
  }

  {
    std::lock_guard lock(ui_mutex);
    gui.prepare_dialog_windows(ui_state);
  }
  gui.use_main_context();
  GbcImGui::new_frame();

  bool request_quit = false;
  bool ff_local = false;
  float menu_bar_height{};
  float status_bar_height{};
  {
    std::lock_guard lock(ui_mutex);
    sync_io_status_to_ui();
    gui.render(ui_state, host);

    if (ui_state.cheats_dirty) {
      ui_state.cheats_dirty = false;
      cheats_revision_.fetch_add(1, std::memory_order_release);
    }
    if (ui_state.cheats_file_dirty) {
      ui_state.cheats_file_dirty = false;
      save_active_cheats_locked();
    }

    if (should_render_attached_dialog(gui, GbcImGui::DialogId::Savestates,
                                      ui_state.show_savestate_manager)) {
      build_savestate_manager_window_locked(false);
    }

    for (const auto &spec : kDebuggerDialogs) {
      if (!should_render_attached_dialog(gui, spec.id, ui_state.*(spec.visible_flag))) {
        continue;
      }
      debugger.render_dialog(spec.id, ui_state, gbc, host.get_renderer(), false);
    }

    poll_zip_choice_response();
    request_quit = ui_state.request_quit;
    if (request_quit) {
      ui_state.request_quit = false;
    }
    ff_local = ui_state.fast_forward;
    menu_bar_height = ui_state.menu_bar_height;
    status_bar_height = ui_state.status_bar_height;
  }

  fast_forward.store(ff_local, std::memory_order_relaxed);
  if (request_quit) {
    running = false;
  }

  if (!startup_window_size_adjusted && menu_bar_height > 0.0f && status_bar_height > 0.0f) {
    if (SDL_Window *window = host.get_window()) {
      const Uint32 flags = SDL_GetWindowFlags(window);
      if (!(flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN))) {
        constexpr int target_w = framebuf_width * scale;
        const int target_h = framebuf_height * scale +
                             static_cast<int>(std::lround(menu_bar_height + status_bar_height));
        int cur_w = 0;
        int cur_h = 0;
        SDL_GetWindowSize(window, &cur_w, &cur_h);
        if (cur_w != target_w || cur_h != target_h) {
          SDL_SetWindowSize(window, target_w, target_h);
        }
      }
    }
    startup_window_size_adjusted = true;
  }

  GbcImGui::end_frame();

  host.clear_screen();
  host.draw_texture(menu_bar_height, status_bar_height);
  host.draw_overlay(ImGui::GetDrawData());
  host.present();

  const auto render_detached_dialog = [this]<typename RenderFn>(const GbcImGui::DialogId id,
                                                                RenderFn &&render_fn) {
    std::lock_guard lock(ui_mutex);
    if (!gui.use_detached_dialog_context(id, ui_state)) {
      return;
    }

    GbcImGui::new_frame();
    render_fn();
    GbcImGui::end_frame();
    gui.present_detached_dialog(id);
  };

  render_detached_dialog(GbcImGui::DialogId::Settings,
                         [&] { gui.render_dialog(GbcImGui::DialogId::Settings, ui_state, host); });
  render_detached_dialog(GbcImGui::DialogId::Cheats,
                         [&] { gui.render_dialog(GbcImGui::DialogId::Cheats, ui_state, host); });
  render_detached_dialog(GbcImGui::DialogId::Keybinds,
                         [&] { gui.render_dialog(GbcImGui::DialogId::Keybinds, ui_state, host); });
  render_detached_dialog(GbcImGui::DialogId::Savestates,
                         [&] { build_savestate_manager_window_locked(true); });

  for (const auto &spec : kDebuggerDialogs) {
    render_detached_dialog(spec.id, [&] {
      debugger.render_dialog(spec.id, ui_state, gbc, gui.active_renderer(), true);
    });
  }

  gui.use_main_context();
}

bool SDLCALL SDL3Frontend::event_watcher(void *userdata, const SDL_Event *event) {
  if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event->type == SDL_EVENT_WINDOW_MOVED ||
      event->type == SDL_EVENT_WINDOW_EXPOSED) {
    static auto last_draw = Clock::now();
    const int min_interval_ms = (event->type == SDL_EVENT_WINDOW_MOVED) ? 33 : 16;

    if (const auto now = Clock::now();
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw).count() >=
        min_interval_ms) {
      auto *self = static_cast<SDL3Frontend *>(userdata);
      const auto now_ns =
          std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

      // The watcher runs inside SDL's event dispatch path, so it should never
      // block waiting for UI work. If the UI thread is mid-update, skip the
      // forced draw and let the normal render loop catch up.
      if (!self->ui_mutex.try_lock()) {
        last_draw = now;
        return true;
      }
      self->ui_mutex.unlock();

      self->render_frame();
      self->last_forced_redraw_ns.store(now_ns, std::memory_order_relaxed);
      last_draw = now;
    }
  }

  return true;
}
