#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "breakpoint.hpp"
#include "emu_types.hpp"
#include <functional>
#include <unordered_map>

namespace Debug {

class Debugger {
public:
  explicit Debugger(std::function<BreakReason()> callback);
  void eval(const addr_t addr, Debug::BreakReason reason);
  void request_stop(Debug::BreakReason reason);

  const std::unordered_map<addr_t, Breakpoint> &get_breakpoints() const;
  void breakpoint_add(const addr_t addr, Debug::BreakReason reason);
  void breakpoint_del(const addr_t addr);

private:
  std::function<BreakReason()> on_brk_callback{};
  std::unordered_map<addr_t, Breakpoint> bp_map{};
  BreakReason reason_{};
};

} // namespace Debug

#endif // DEBUGGER_H
