#ifndef GBC_DEBUGGER_HPP
#define GBC_DEBUGGER_HPP

#pragma once
#include "SDL3/SDL_render.h"
#include "common.hpp"
#include "frontend/sdl3/sdl_host.hpp"
#include "gbc.hpp"
#include <condition_variable>
#include <mutex>

struct DebugContext {
  Debug::BreakReason reason{Debug::BRK_CONTINUE};
  SDL_Texture *tile_data_texture{};

  std::string sys_state{};
  std::string cpu_state{};
  std::string ppu_state{};
  std::string ie_state{};
  std::string if_state{};
  std::string disasm{};
  bool stopped{false};
};

struct BreakpointPrompt {
  addr_t addr{0};
  bool read{false};
  bool write{false};
  bool execute{false};
  bool show{false};
};

class DebuggerImGui {
public:
  void init(SDLHost &host);
  void render(UiState &state, const std::unique_ptr<GameBoyColor> &core);
  Debug::BreakReason on_breakpoint(const std::stop_token &st,
                                   const std::unique_ptr<GameBoyColor> &core);
  void update_state_from_core(const std::unique_ptr<GameBoyColor> &core);
  void forward_stop(const std::unique_ptr<GameBoyColor> &core) const;
  void request_stop();

private:
  void build_debug_window(UiState &state);
  void build_breakpoints_window(UiState &state,
                                const std::unique_ptr<GameBoyColor> &core);
  void
  build_config_breakpoint_window(const std::unique_ptr<GameBoyColor> &core);
  void build_ppu_viewer_window(UiState &state,
                               const std::unique_ptr<GameBoyColor> &core);

  void read_vram_tile_data(const std::unique_ptr<GameBoyColor> &core);
  std::vector<std::uint32_t> tile_data_buf;
  DebugContext ctx; // Debugger context

  mutable std::mutex dbg_mutex;
  std::condition_variable dbg_cv;
  BreakpointPrompt bp_prompt{};
};

#endif // GBC_DEBUGGER_HPP
