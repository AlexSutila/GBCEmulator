#ifndef __BREAKPOINT_H
#define __BREAKPOINT_H

#include "emu_types.hpp"
#include <cstdint>
#include <string>

namespace Debug {

enum BreakReason : std::uint32_t {
  BRK_CONTINUE = 0,
  // User configured or hardwardware specified reasons
  BRK_ADDRESS_EXECUTED = 1 << 1,
  BRK_ADDRESS_READ = 1 << 2,
  BRK_ADDRESS_WRITTEN = 1 << 3,
  // Hardware specified reasons only
  BRK_STEP_INSTRUCTION = 1 << 4,
};

constexpr BreakReason operator|(BreakReason a, BreakReason b) {
  return static_cast<BreakReason>(static_cast<uint8_t>(a) |
                                  static_cast<uint8_t>(b));
}

constexpr bool operator&(BreakReason a, BreakReason b) {
  return static_cast<uint8_t>(a) & static_cast<uint8_t>(b);
}

constexpr auto BRK_STOPPED_BY_UI = BRK_STEP_INSTRUCTION;

class Breakpoint {
public:
  explicit Breakpoint(BreakReason reason_flags, addr_t watch_addr);
  bool eval(BreakReason reason_flags) const;
  bool has_flag(BreakReason flag) const;

  [[nodiscard]] std::string to_string() const;

private:
  BreakReason reasons{};
  const addr_t addr;
};

}; // namespace Debug

#endif // __BREAKPOINT_H
