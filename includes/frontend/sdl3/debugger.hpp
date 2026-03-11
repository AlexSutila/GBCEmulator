#ifndef GBC_SDL3_DEBUGGER_HPP
#define GBC_SDL3_DEBUGGER_HPP

#pragma once
#include "SDL3/SDL_render.h"
#include "common.hpp"
#include "frontend/sdl3/gui.hpp"
#include "gbc.hpp"
#include <array>
#include <condition_variable>
#include <mutex>

struct DebugContext {
  Debug::BreakReason reason{Debug::BRK_CONTINUE};
  std::vector<byte_t> bus_content{}; // use read_byte_safe()
  addr_t bus_content_base_addr{};
  std::string oam_dma_state{};
  std::string vdma_state{};
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
  ~DebuggerImGui();
  void init();
  void render(UiState &state, const std::unique_ptr<GameBoyColor> &core, SDL_Renderer *renderer,
              bool fill_viewport = false);
  void render_dialog(GbcImGui::DialogId id, UiState &state,
                     const std::unique_ptr<GameBoyColor> &core, SDL_Renderer *renderer,
                     bool fill_viewport = false);
  Debug::BreakReason on_breakpoint(const std::stop_token &st,
                                   const std::unique_ptr<GameBoyColor> &core);
  void update_state_from_core(const std::unique_ptr<GameBoyColor> &core);
  void forward_stop(const std::unique_ptr<GameBoyColor> &core) const;
  void request_stop();

private:
  void build_debug_window(UiState &state, bool fill_viewport);
  void build_memory_viewer_window(UiState &state, const std::unique_ptr<GameBoyColor> &core,
                                  bool fill_viewport);
  void build_breakpoints_window(UiState &state, const std::unique_ptr<GameBoyColor> &core,
                                bool fill_viewport);
  void build_config_breakpoint_window(const std::unique_ptr<GameBoyColor> &core);
  void build_ppu_viewer_window(UiState &state, SDL_Renderer *renderer, bool fill_viewport);

  void ensure_tile_data_textures(SDL_Renderer *renderer);
  void destroy_tile_data_textures();
  void render_vram_tile_data(size_t vram_bank_idx, SDL_Renderer *renderer);
  void read_vram_tile_data(const std::unique_ptr<GameBoyColor> &core, std::size_t vram_bank_idx);
  void read_bus_data(const std::unique_ptr<GameBoyColor> &core, addr_t start_addr);
  std::array<std::vector<std::uint32_t>, 2> tile_data_buf{};
  SDL_Renderer *tile_data_renderer_{nullptr};
  std::array<SDL_Texture *, 2> tile_data_textures_{};
  DebugContext ctx; // Debugger context

  mutable std::mutex dbg_mutex;
  std::condition_variable dbg_cv;
  BreakpointPrompt bp_prompt{};
};

#endif // GBC_SDL3_DEBUGGER_HPP
