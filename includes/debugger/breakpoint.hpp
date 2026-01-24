#ifndef __BREAKPOINT_H
#define __BREAKPOINT_H

#include "emu_types.hpp"
#include <cstdint>
#include <string>

namespace Debug {

enum BreakReason : std::uint32_t {
  // User configured or hardwardware specified reasons
  BRK_ADDRESS_EXECUTED = 1 << 1,
  BRK_ADDRESS_READ = 1 << 2,
  BRK_ADDRESS_WRITTEN = 1 << 3,
  // Hardware specified reasons only
  BRK_STEP_INSTRUCTION = 1 << 4,
};

class Breakpoint {
public:
  explicit Breakpoint(BreakReason reason_flags, addr_t watch_addr);
  bool eval(BreakReason reason_flags) const;

  [[nodiscard]] std::string to_string() const;

private:
  BreakReason reasons{};
  const addr_t addr;
  const bool hardware_defined{};
};

}; // namespace Debug

#endif // __BREAKPOINT_H
