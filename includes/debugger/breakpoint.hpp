#ifndef GBC_BREAKPOINT_HPP
#define GBC_BREAKPOINT_HPP

#include "emu_types.hpp"
#include "schedule.hpp"
#include <cstdint>
#include <string>
#include <variant>

namespace Debug {

/**
 * A comprehensive list of reasons for a breakpoint to stop execution. User
 * configured breakpoints are, obviously, configurable and specified via UI.
 * Other reasons include the actual hardware event that is being evaluated.
 * -------------------------------------------------------------------------
 * Reasons are treated like bitmasks, hence, sometimes multiple reasons may
 * be required for a given breakpoint to stop execution. Details vary based
 * on the nature of each breakpoint.
 */
enum BreakReason : std::uint32_t {
  BRK_CONTINUE = 0,
  // User configured or hardware specified reasons
  BRK_ADDRESS_EXECUTED = 1 << 1,
  BRK_ADDRESS_READ = 1 << 2,
  BRK_ADDRESS_WRITTEN = 1 << 3,
  // User configured scheduler event reasons
  BRK_EVENT_QUEUED = 1 << 4,
  // Hardware specified reasons only
  BRK_STEP_CLOCK_CYCLE = 1 << 5,
  BRK_STEP_INSTRUCTION = 1 << 6,
  BRK_STEP_SCANLINE = 1 << 7,
  BRK_STEP_FRAME = 1 << 8,
};

constexpr BreakReason operator|(const BreakReason a, const BreakReason b) {
  return static_cast<BreakReason>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr bool operator&(const BreakReason a, const BreakReason b) {
  return static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b);
}

/* The rationale here, is the user likely expects to see the `current` CPU state
 * right as they press `break` (or whatever it is, based on frontend details).
 * Pausing on other events might be confusing, and this event happens frequently
 * enough to still come across as seamless to the naked eye. */
constexpr auto BRK_STOPPED_BY_UI = BRK_STEP_INSTRUCTION;

struct Context {
  BreakReason reason;
  time_type time;

  // Additional context depends on breakpoint type
  std::variant<std::monostate, event, addr_t> data;
};

class Breakpoint {
public:
  explicit Breakpoint(BreakReason reason_flags, addr_t watch_addr);
  explicit Breakpoint(BreakReason reason_flags, event e);

  [[nodiscard]] bool eval(BreakReason reason_flags) const;
  [[nodiscard]] bool has_flag(BreakReason flag) const;

  [[nodiscard]] std::string to_string() const;

private:
  std::variant<std::monostate, event, addr_t> context;
  BreakReason reasons{};
};

}; // namespace Debug

#endif // GBC_BREAKPOINT_HPP
