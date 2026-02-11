#include "frontend/sdl3/debugger.hpp"
#include "SDL3/SDL_pixels.h"
#include "SDL3/SDL_render.h"
#include "debugger/breakpoint.hpp"
#include "debugger/print.hpp"
#include "frontend/sdl3/sdl_host.hpp"
#include "ppu/palette.hpp"
#include <algorithm>
#include <imgui.h>
#include <mutex>
#include <stdexcept>

constexpr auto tile_data_height_tiles = 12;
constexpr auto tile_data_width_tiles = 32;
constexpr auto tile_data_height_px = tile_data_height_tiles * 8;
constexpr auto tile_data_width_px = tile_data_width_tiles * 8;
constexpr auto black = 0xFF000000;

void DebuggerImGui::init(SDLHost &host) {
  ctx.tile_data_texture = SDL_CreateTexture(
      host.get_renderer(), SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, tile_data_width_px, tile_data_height_px);
  if (!ctx.tile_data_texture)
    throw std::runtime_error("Failed to initialize debug textures");

  tile_data_buf.resize(tile_data_height_px * tile_data_width_px);
  std::fill(tile_data_buf.begin(), tile_data_buf.end(), black);
}

// This is a public entry point called from the main GUI render loop
// Replaces the build_ui functionality
void DebuggerImGui::render(UiState &state,
                           const std::unique_ptr<GameBoyColor> &core) {
  if (state.show_debug)
    build_debug_window(state);
  if (state.show_breakpoints)
    build_breakpoints_window(state, core);
  if (state.show_ppu_viewer)
    build_ppu_viewer_window(state, core);
}

// Called by the emulator thread when a breakpoint is hit
Debug::BreakReason
DebuggerImGui::on_breakpoint(const std::stop_token &st,
                             const std::unique_ptr<GameBoyColor> &core) {
  std::unique_lock lock(dbg_mutex);
  ctx.stopped = true;
  update_state_from_core(core);
  dbg_cv.wait(lock, [this, st] { // Avoid polling loops
    return !ctx.stopped || st.stop_requested();
  });
  return st.stop_requested() ? Debug::BRK_CONTINUE : ctx.reason;
}

// Call from main thread to update visual state
void DebuggerImGui::update_state_from_core(
    const std::unique_ptr<GameBoyColor> &core) {
  ctx.sys_state = Debug::to_string(core->get_sys());
  ctx.cpu_state = Debug::to_string(core->get_cpu()->get_state());
  ctx.ppu_state = Debug::to_string(core->get_ppu()->get_state());
  ctx.disasm = core->get_cpu()->disasm();

  const InterruptBits *const ie_reg = dynamic_cast<InterruptBits *>(
      core->get_bus()->get_mmio(IORegisterMapping::MMIO_INT_ENABLE));
  const InterruptBits *const if_reg = dynamic_cast<InterruptBits *>(
      core->get_bus()->get_mmio(IORegisterMapping::MMIO_INT_FLAGS));
  ctx.ie_state = Debug::to_string(*ie_reg);
  ctx.if_state = Debug::to_string(*if_reg);
}

void DebuggerImGui::forward_stop(
    const std::unique_ptr<GameBoyColor> &core) const {
  std::lock_guard lock(dbg_mutex);
  if (ctx.stopped)
    core->get_debugger()->request_stop(ctx.reason);
}

void DebuggerImGui::request_stop() {
  {
    std::lock_guard lock(dbg_mutex);
    ctx.stopped = false;
  }
  dbg_cv.notify_all();
}

/* ImGui constructions */
void DebuggerImGui::build_debug_window(UiState &state) {
  ImGui::Begin("Debug", &state.show_debug);

  ImGui::SeparatorText("System State");
  ImGui::Text("Disassembly: %s", ctx.disasm.c_str());
  ImGui::Text(" %s", ctx.sys_state.c_str());

  ImGui::SeparatorText("Processor State");
  ImGui::Text("%s", ctx.cpu_state.c_str());
  ImGui::SameLine();
  ImGui::Text("IF: %s", ctx.if_state.c_str());
  ImGui::SameLine();
  ImGui::Text("IE: %s", ctx.ie_state.c_str());

  ImGui::SeparatorText("Control Flow");
  if (ImGui::Button("Break")) {
    std::lock_guard lock(dbg_mutex);
    ctx.reason = Debug::BRK_STOPPED_BY_UI;
    ctx.stopped = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Step Cycle")) {
    {
      std::lock_guard lock(dbg_mutex);
      ctx.reason = Debug::BRK_STEP_CLOCK_CYCLE;
      ctx.stopped = false;
    }
    dbg_cv.notify_one();
  }
  ImGui::SameLine();
  if (ImGui::Button("Step Instr")) {
    {
      std::lock_guard lock(dbg_mutex);
      ctx.reason = Debug::BRK_STEP_INSTRUCTION;
      ctx.stopped = false;
    }
    dbg_cv.notify_one();
  }
  ImGui::SameLine();
  if (ImGui::Button("Step Scanline")) {
    {
      std::lock_guard lock(dbg_mutex);
      ctx.reason = Debug::BRK_STEP_SCANLINE;
      ctx.stopped = false;
    }
    dbg_cv.notify_one();
  }
  ImGui::SameLine();
  if (ImGui::Button("Step Frame")) {
    {
      std::lock_guard lock(dbg_mutex);
      ctx.reason = Debug::BRK_STEP_FRAME;
      ctx.stopped = false;
    }
    dbg_cv.notify_one();
  }
  ImGui::SameLine();
  if (ImGui::Button("Continue")) {
    {
      std::lock_guard lock(dbg_mutex);
      ctx.reason = Debug::BRK_CONTINUE;
      ctx.stopped = false;
    }
    dbg_cv.notify_one();
  }
  ImGui::End();
}

void DebuggerImGui::build_breakpoints_window(
    UiState &state, const std::unique_ptr<GameBoyColor> &core) {
  std::lock_guard lock(dbg_mutex);
  ImGui::Begin("Breakpoints", &state.show_breakpoints);
  ImGui::SeparatorText("Breakpoints");
  auto &debugger = core->get_debugger();

  if (!debugger.has_value()) {
    ImGui::Text("Debugger not configured");
    ImGui::End();
    return;
  }

  for (const auto bps = debugger->get_breakpoints();
       const auto &[addr, bp] : bps) {
    ImGui::PushID(addr);

    ImGui::Text("%s", bp.to_string().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Edit")) {
      bp_prompt.execute = bp.has_flag(Debug::BRK_ADDRESS_EXECUTED);
      bp_prompt.read = bp.has_flag(Debug::BRK_ADDRESS_READ);
      bp_prompt.write = bp.has_flag(Debug::BRK_ADDRESS_WRITTEN);
      bp_prompt.addr = addr;
      bp_prompt.show = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove"))
      debugger->breakpoint_del(addr);
    ImGui::PopID();
  }
  if (ImGui::Button("Add"))
    bp_prompt.show = true;

  if (bp_prompt.show) {
    ImGui::OpenPopup("Configure breakpoint");
    build_config_breakpoint_window(core);
  }
  ImGui::End();
}

void DebuggerImGui::build_ppu_viewer_window(
    UiState &state, const std::unique_ptr<GameBoyColor> &core) {
  std::lock_guard lock(dbg_mutex);
  constexpr float scale = 1.5f;

  // Update tile data texture
  read_vram_tile_data(core);
  SDL_UpdateTexture(ctx.tile_data_texture, nullptr, tile_data_buf.data(),
                    tile_data_width_px * sizeof(std::uint32_t));

  ImGui::Begin("Pixel Processor Viewer", &state.show_ppu_viewer);
  ImGui::SeparatorText("Pixel Processor State");
  ImGui::Text("%s", ctx.ppu_state.c_str());

  // Render tile data to debug view
  ImGui::SeparatorText("Tile Data");
  ImVec2 tile_data_size(tile_data_width_px * scale,
                        tile_data_height_px * scale);
  ImGui::Image((ImTextureID)ctx.tile_data_texture, tile_data_size);

  ImGui::End();
}

void DebuggerImGui::build_config_breakpoint_window(
    const std::unique_ptr<GameBoyColor> &core) {
  if (ImGui::BeginPopupModal("Configure breakpoint", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {

    ImGui::InputScalar("Address", ImGuiDataType_U16, &bp_prompt.addr, nullptr,
                       nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::Checkbox("Exec", &bp_prompt.execute);
    ImGui::SameLine();
    ImGui::Checkbox("Read", &bp_prompt.read);
    ImGui::SameLine();
    ImGui::Checkbox("Write", &bp_prompt.write);
    ImGui::Separator();

    if (ImGui::Button("OK", ImVec2(120, 0))) {
      Debug::BreakReason reason{};
      if (bp_prompt.execute)
        reason = reason | Debug::BRK_ADDRESS_EXECUTED;
      if (bp_prompt.write)
        reason = reason | Debug::BRK_ADDRESS_WRITTEN;
      if (bp_prompt.read)
        reason = reason | Debug::BRK_ADDRESS_READ;
      core->get_debugger()->breakpoint_add(bp_prompt.addr, reason);
      ImGui::CloseCurrentPopup();
      bp_prompt.show = false;
    }
    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
      bp_prompt.show = false;
    }

    ImGui::EndPopup();
  }
}

void DebuggerImGui::read_vram_tile_data(
    const std::unique_ptr<GameBoyColor> &core) {
  constexpr std::size_t tile_width = 8;
  constexpr std::size_t tile_height = 8;
  constexpr std::size_t bytes_per_tile = 16;
  constexpr std::size_t tiles_per_row = tile_data_width_tiles;
  constexpr std::size_t tile_count = 384;

  // TODO: Need to hit the second VRAM bank
  const auto &vram = core->get_bus()->get_vram();
  const auto &bank_0 = vram.at(0);

  // For get any PPU timing, just render what is in VRAM as is
  for (std::size_t tile = 0; tile < tile_count; ++tile) {
    const std::size_t tile_x = (tile % tiles_per_row) * tile_width;
    const std::size_t tile_y = (tile / tiles_per_row) * tile_height;
    const std::size_t base = tile * bytes_per_tile;
    for (std::size_t row = 0; row < tile_height; ++row) {
      const byte_t lo = bank_0[base + row * 2];
      const byte_t hi = bank_0[base + row * 2 + 1];

      for (std::size_t col = 0; col < tile_width; ++col) {
        const std::size_t bit = 7 - col;
        const byte_t color_idx = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);

        const std::size_t x = tile_x + col, y = tile_y + row;
        tile_data_buf[y * tile_data_width_px + x] = get_mono_color(color_idx);
      }
    }
  }
}
