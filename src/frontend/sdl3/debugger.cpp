#include "frontend/sdl3/debugger.hpp"
#include <imgui.h>
#include "debugger/print.hpp"


// This is a public entry point called from the main GUI render loop
// Replaces the build_ui functionality
void DebuggerImGui::render(UiState& state, const std::unique_ptr<GameBoyColor>& core) {
  if (state.show_debug) {
    build_debug_window(state);
  }
  if (state.show_breakpoints) {
    build_breakpoints_window(state, core);
  }
}

// Called by the emulator thread when a breakpoint is hit
Debug::BreakReason DebuggerImGui::on_breakpoint(const std::stop_token& st, const std::unique_ptr<GameBoyColor>& core) {
  std::unique_lock lock(dbg_mutex);
  ctx.stopped = true;
  update_state_from_core(core);
  dbg_cv.wait(lock, [this, st] { // Avoid polling loops
    return !ctx.stopped || st.stop_requested();
  });
  return st.stop_requested() ? Debug::BRK_CONTINUE : ctx.reason;
}

// Call from main thread to update visual state
void DebuggerImGui::update_state_from_core(const std::unique_ptr<GameBoyColor>& core) {
  ctx.sys_state = Debug::to_string(core->get_sys());
  ctx.cpu_state = Debug::to_string(core->get_cpu()->get_state());
  ctx.disasm = core->get_cpu()->disasm();

  const InterruptBits *const ie_reg = dynamic_cast<InterruptBits *>(
      core->get_bus()->get_mmio(IORegisterMapping::MMIO_INT_ENABLE));
  const InterruptBits *const if_reg = dynamic_cast<InterruptBits *>(
      core->get_bus()->get_mmio(IORegisterMapping::MMIO_INT_FLAGS));
  ctx.ie_state = Debug::to_string(*ie_reg);
  ctx.if_state = Debug::to_string(*if_reg);
}

void DebuggerImGui::forward_stop(const std::unique_ptr<GameBoyColor>& core) const {
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
  if (ImGui::Button("Step Instruction")) {
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

void DebuggerImGui::build_breakpoints_window(UiState& state, const std::unique_ptr<GameBoyColor>& core) {
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

void DebuggerImGui::build_config_breakpoint_window(const std::unique_ptr<GameBoyColor>& core) {
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