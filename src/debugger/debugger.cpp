#include "debugger/debugger.hpp"
#include "debugger/breakpoint.hpp"

namespace Debug {

Debugger::Debugger(std::function<BreakReason()> on_break_callback)
    : on_brk_callback(on_break_callback) {
  bp_map.clear();
}

void Debugger::eval(const addr_t addr, Debug::BreakReason reason) {
  // Short circuit evaluation can prevent lookup to help performance
  if ((reason & reason_) != 0 ||
      (bp_map.contains(addr) && bp_map.at(addr).eval(reason))) [[unlikely]]
    reason_ = on_brk_callback();
}

const std::unordered_map<addr_t, Breakpoint> &
Debugger::get_breakpoints() const {
  return bp_map;
}

void Debugger::breakpoint_add(const addr_t addr, Debug::BreakReason reason) {
  bp_map.erase(addr);
  bp_map.emplace(addr, Breakpoint(reason, addr));
}

void Debugger::breakpoint_del(const addr_t addr) {
  if (bp_map.contains(addr))
    bp_map.erase(addr);
}

void Debugger::request_stop(Debug::BreakReason reason) { reason_ = reason; }

} // namespace Debug
