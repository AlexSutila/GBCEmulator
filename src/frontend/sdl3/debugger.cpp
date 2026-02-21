#include "frontend/sdl3/debugger.hpp"
#include "SDL3/SDL_pixels.h"
#include "SDL3/SDL_render.h"
#include "debugger/breakpoint.hpp"
#include "debugger/print.hpp"
#include "frontend/sdl3/sdl_host.hpp"
#include "ppu/palette.hpp"
#include <algorithm>
#include <cassert>
#include <imgui.h>
#include <mutex>
#include <stdexcept>

constexpr auto hex_viewer_bytes_shown = 0x10 * 0x10; // Don't mess with this
static_assert(hex_viewer_bytes_shown % 0x10 == 0, "Should be a factor of 16");

constexpr auto tile_data_height_tiles = 24;
constexpr auto tile_data_width_tiles = 16;
constexpr auto tile_data_height_px = tile_data_height_tiles * 8;
constexpr auto tile_data_width_px = tile_data_width_tiles * 8;
constexpr auto black = 0xFF000000;

void DebuggerImGui::init(const SDLHost &host) {
  for (auto &texture : ctx.tile_data_texture) {
    texture = SDL_CreateTexture(host.get_renderer(), SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, tile_data_width_px,
                                tile_data_height_px);
    if (!texture)
      throw std::runtime_error("Failed to initialize debug textures");
  }

  // Clears buffers used to update textures
  for (auto &buf : tile_data_buf) {
    buf.resize(tile_data_height_px * tile_data_width_px);
    std::ranges::fill(buf, black);
  }

  // Allocate heap space for the hex view memory reader
  ctx.bus_content.resize(hex_viewer_bytes_shown);
}

// This is a public entry point called from the main GUI render loop
// Replaces the build_ui functionality
void DebuggerImGui::render(UiState &state,
                           const std::unique_ptr<GameBoyColor> &core) {
  if (state.show_main_debug_viewer)
    build_debug_window(state);
  if (state.show_memory_viewer)
    build_memory_viewer_window(state, core);
  if (state.show_breakpoints)
    build_breakpoints_window(state, core);
  if (state.show_ppu_viewer)
    build_ppu_viewer_window(state);
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
  const auto address_bus = core->get_bus();

  // Address bus relevant information
  ctx.oam_dma_state = Debug::to_string(address_bus->get_oam_dma().get_state());
  ctx.vdma_state = Debug::to_string(address_bus->get_vdma().get_state());
  read_bus_data(core, ctx.bus_content_base_addr & 0xFF00);

  // Interrupt enable bits and flags
  const InterruptBits *const ie_reg = dynamic_cast<InterruptBits *>(
      address_bus->get_mmio(IORegisterMapping::MMIO_INT_ENABLE));
  const InterruptBits *const if_reg = dynamic_cast<InterruptBits *>(
      address_bus->get_mmio(IORegisterMapping::MMIO_INT_FLAGS));
  ctx.ie_state = Debug::to_string(*ie_reg);
  ctx.if_state = Debug::to_string(*if_reg);

  // Populates the tile data buffers in ctx
  read_vram_tile_data(core, 0);
  read_vram_tile_data(core, 1);
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
  ImGui::Begin("Debug", &state.show_main_debug_viewer);

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

void DebuggerImGui::build_memory_viewer_window(
    UiState &state, const std::unique_ptr<GameBoyColor> &core) {
  constexpr auto mask = 0xFF00;
  constexpr auto increment = 0x100;
  std::lock_guard lock(dbg_mutex);

  ImGui::Begin("Memory Viewer", &state.show_memory_viewer);
  ImGui::SeparatorText("Direct Memory Access");
  ImGui::Text("%s", ctx.oam_dma_state.c_str());
  ImGui::SameLine();
  ImGui::Text("%s", ctx.vdma_state.c_str());

  // Yeah... you read that right >:)
  ImGui::SeparatorText("Main Address Bus View");
  const std::string sexy_ahh_hex_view = Debug::create_hex_view(
      state.hex_view_base_addr & 0xFF00, ctx.bus_content);
  ImGui::Text("%s", sexy_ahh_hex_view.c_str());

  ImGui::InputScalar("##mem_view_addr", ImGuiDataType_U16,
                     &ctx.bus_content_base_addr, nullptr, nullptr, "%04X",
                     ImGuiInputTextFlags_CharsHexadecimal);
  ImGui::SameLine();
  if (ImGui::Button("GoTo")) {
    read_bus_data(core, ctx.bus_content_base_addr & mask);
    state.hex_view_base_addr = ctx.bus_content_base_addr & mask;
  }
  ImGui::SameLine();

  if (ImGui::Button("Next")) { // Overflow is allowed intentionally
    state.hex_view_base_addr = (state.hex_view_base_addr & mask) + increment;
    read_bus_data(core, state.hex_view_base_addr & mask);
    ctx.bus_content_base_addr = state.hex_view_base_addr;
  }
  ImGui::SameLine();

  if (ImGui::Button("Prev")) { // Underflow is also allowed intentionally
    state.hex_view_base_addr = (state.hex_view_base_addr & mask) - increment;
    read_bus_data(core, state.hex_view_base_addr & mask);
    ctx.bus_content_base_addr = state.hex_view_base_addr;
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

void DebuggerImGui::build_ppu_viewer_window(UiState &state) const {
  std::lock_guard lock(dbg_mutex);

  ImGui::Begin("Pixel Processor Viewer", &state.show_ppu_viewer);
  ImGui::SeparatorText("Pixel Processor State");
  ImGui::Text("%s", ctx.ppu_state.c_str());

  // Render tile data to debug view for both banks
  ImGui::SeparatorText("Tile Data: (VRAM banks 0, 1)");
  render_vram_tile_data(0);
  ImGui::SameLine();
  render_vram_tile_data(1);
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

void DebuggerImGui::render_vram_tile_data(const size_t vram_bank_idx) const {
  constexpr float scale = 1.5f; // Lol, hardcoded bc idc
  constexpr ImVec2 size(tile_data_width_px * scale,
                        tile_data_height_px * scale);

  // Fetch tile data from VRAM, as is, and render to texture
  SDL_UpdateTexture(ctx.tile_data_texture.at(vram_bank_idx), nullptr,
                    tile_data_buf.at(vram_bank_idx).data(),
                    tile_data_width_px * sizeof(std::uint32_t));
  ImGui::Image(ctx.tile_data_texture.at(vram_bank_idx), size);
}

void DebuggerImGui::read_vram_tile_data(
    const std::unique_ptr<GameBoyColor> &core,
    const std::size_t vram_bank_idx) {
  constexpr std::size_t tiles_per_row = tile_data_width_tiles;
  constexpr std::size_t tile_count = 384;
  assert(vram_bank_idx >= 0 && vram_bank_idx <= 1);

  // Isolate to a single VRAM bank
  const auto &vram_bank = core->get_bus()->get_vram().at(vram_bank_idx);
  auto &buf = tile_data_buf.at(vram_bank_idx);

  // For get any PPU timing, just copy what is in VRAM as is
  for (std::size_t tile = 0; tile < tile_count; ++tile) {
    constexpr std::size_t bytes_per_tile = 16;
    constexpr std::size_t tile_height = 8;
    constexpr std::size_t tile_width = 8;
    const std::size_t tile_x = tile % tiles_per_row * tile_width;
    const std::size_t tile_y = tile / tiles_per_row * tile_height;
    const std::size_t base = tile * bytes_per_tile;
    for (std::size_t row = 0; row < tile_height; ++row) {
      const byte_t lo = vram_bank[base + row * 2];
      const byte_t hi = vram_bank[base + row * 2 + 1];

      for (std::size_t col = 0; col < tile_width; ++col) {
        const std::size_t bit = 7 - col;
        const byte_t color_idx = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);

        const std::size_t x = tile_x + col, y = tile_y + row;
        buf[y * tile_data_width_px + x] = get_mono_color(color_idx);
      }
    }
  }
}

void DebuggerImGui::read_bus_data(const std::unique_ptr<GameBoyColor> &core,
                                  const addr_t start_addr) {
  const auto bus =
      core->get_bus(); // Be sure not to mess with memory mapped regs
  for (std::size_t offset{0}; offset < hex_viewer_bytes_shown; ++offset) {
    const addr_t cur_addr_full = (start_addr + offset) & 0xFFFF;
    ctx.bus_content.at(offset) = bus->read_byte_safe(cur_addr_full);
  }
}
