#include <utility>

#include "debugger/breakpoint.hpp"
#include "debugger/debugger.hpp"

namespace Debug {

Debugger::Debugger(std::function<BreakReason(Context)> callback)
    : on_brk_callback(std::move(callback)), reason_(BRK_CONTINUE) {
  bp_map.clear();
}

void Debugger::eval(time_type time, const addr_t addr, const BreakReason reason) {
  // Short circuit evaluation can prevent lookup to help performance
  if ((reason & reason_) != 0 || (bp_map.contains(addr) && bp_map.at(addr).eval(reason)))
      [[unlikely]] {
    const Context ctx = {
        .reason = reason,
        .time = time,
        .data = addr,
    };
    reason_ = on_brk_callback(ctx);
  }
}

void Debugger::eval(time_type time, const BreakReason reason) {
  if ((reason & reason_) != 0) [[unlikely]] {
    const Context ctx = {
        .reason = reason,
        .time = time,
        .data = std::monostate(),
    };
    reason_ = on_brk_callback(ctx);
  }
}

const std::unordered_map<addr_t, Breakpoint> &Debugger::get_breakpoints() const { return bp_map; }

void Debugger::breakpoint_add(const addr_t addr, const BreakReason reason) {
  bp_map.erase(addr);
  bp_map.emplace(addr, Breakpoint(reason, addr));
}

void Debugger::breakpoint_del(const addr_t addr) {
  if (bp_map.contains(addr))
    bp_map.erase(addr);
}

void Debugger::request_stop(const BreakReason reason) { reason_ = reason; }

void Debuggable::try_brk(time_type time, const addr_t addr, const BreakReason reason) const {
  if (debugger_.has_value())
    debugger_->eval(time, addr, reason);
}

void Debuggable::try_brk(time_type time, const BreakReason reason) const {
  if (debugger_.has_value())
    debugger_->eval(time, reason);
}

} // namespace Debug
