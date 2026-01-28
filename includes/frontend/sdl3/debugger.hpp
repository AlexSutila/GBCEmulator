#ifndef GBC_DEBUGGER_HPP
#define GBC_DEBUGGER_HPP

#pragma once
#include "common.hpp"
#include "debugger/debugger.hpp"
#include "gbc.hpp"
#include <mutex>
#include <condition_variable>

struct DebugContext {
  Debug::BreakReason reason{Debug::BRK_CONTINUE};
  std::string sys_state{};
  std::string cpu_state{};
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
  void render(UiState& state, const std::unique_ptr<GameBoyColor>& core);
  Debug::BreakReason on_breakpoint(const std::stop_token& st, const std::unique_ptr<GameBoyColor>& core);
  void update_state_from_core(const std::unique_ptr<GameBoyColor>& core);
  void forward_stop(const std::unique_ptr<GameBoyColor>& core) const;
  void request_stop();

private:
  void build_debug_window(UiState& state);
  void build_breakpoints_window(UiState& state, const std::unique_ptr<GameBoyColor>& core);
  void build_config_breakpoint_window(const std::unique_ptr<GameBoyColor>& core);

  DebugContext ctx;
  mutable std::mutex dbg_mutex;
  std::condition_variable dbg_cv;
  BreakpointPrompt bp_prompt{};
};

#endif //GBC_DEBUGGER_HPP