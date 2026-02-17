#ifndef GBC_DEBUGGER_HPP
#define GBC_DEBUGGER_HPP

#include "breakpoint.hpp"
#include "emu_types.hpp"
#include <functional>
#include <optional>
#include <unordered_map>

namespace Debug {

/**
 * The top level debugger class, responsible for breakpoint maintenance and
 * evaluation. Configuration at the top level is entirely optional. Breakpoints
 * will invoke a callback on hit, and actually pausing the emulator is the sole
 * responsibility of the callback, not the Debugger.
 */
class Debugger {
public:
  explicit Debugger(std::function<BreakReason()> callback);

  // Invoked by components, determines if callback is to be invoked or not
  void eval(addr_t addr, BreakReason reason);
  void eval(BreakReason reason); // For hardware specific breakpoints

  // Invoked by UI to stop execution
  void request_stop(BreakReason reason);

  // Breakpoint maintenance
  [[nodiscard]] const std::unordered_map<addr_t, Breakpoint> &get_breakpoints() const;
  void breakpoint_add(addr_t addr, BreakReason reason);
  void breakpoint_del(addr_t addr);

private:
  std::function<BreakReason()> on_brk_callback{};
  std::unordered_map<addr_t, Breakpoint> bp_map{};
  BreakReason reason_{};
};

/**
 * To be inherited by system components which can trigger breakpoints, example:
 *  - The processor will inherit and invoke try_brk() to do exec breakpoints
 *  - The PPU will inherit and invoke try_brk() to do step scanline breakpoints
 * This is purely a convenience thing to cut down on code repetition.
 */
class Debuggable {
public:
  explicit Debuggable(std::optional<Debugger> &debugger)
      : debugger_(debugger) {}
  void try_brk(addr_t addr, BreakReason reason) const;
  void try_brk(BreakReason reason) const;

private:
  std::optional<Debugger> &debugger_;
};

} // namespace Debug

#endif // GBC_DEBUGGER_HPP
