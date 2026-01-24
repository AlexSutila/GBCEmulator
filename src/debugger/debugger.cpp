#include "debugger/debugger.hpp"
#include "debugger/breakpoint.hpp"

namespace Debug {

Debugger::Debugger(std::function<BreakReason()> on_break_callback)
    : on_brk_callback(on_break_callback) {
  bp_map.clear();
}

void Debugger::eval(const addr_t addr, Debug::BreakReason reason) {
  if ((reason & reason_) != 0)
    reason_ = on_brk_callback();
}

void Debugger::request_stop(Debug::BreakReason reason) {
  reason_ = reason;
}

} // namespace Debug
