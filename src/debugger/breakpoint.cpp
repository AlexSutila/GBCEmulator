#include "debugger/breakpoint.hpp"
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Debug {

Breakpoint::Breakpoint(const BreakReason reason_flags, const addr_t watch_addr)
    : reasons(reason_flags), // Should be read/write/execute only
      addr(watch_addr)       // Address of breakpoint
{
  constexpr auto BRK_ALLOWED_FLAGS =
      BRK_ADDRESS_EXECUTED | BRK_ADDRESS_READ | BRK_ADDRESS_WRITTEN;
  if ((reason_flags & ~BRK_ALLOWED_FLAGS) != 0)
    throw std::runtime_error("Breakpoint::Breakpoint() bad flags");
}

bool Breakpoint::eval(const BreakReason reason_flags) const {
  return (reason_flags & reasons) != 0;
}

bool Breakpoint::has_flag(const BreakReason flag) const {
  return (flag & reasons) != 0;
}

[[nodiscard]] std::string Breakpoint::to_string() const {
  const auto r = static_cast<unsigned>(reasons);
  const bool exec = r & BRK_ADDRESS_EXECUTED;
  const bool read = r & BRK_ADDRESS_READ;
  const bool write = r & BRK_ADDRESS_WRITTEN;

  std::ostringstream out;
  out << "BP " << (exec ? 'E' : '-') << (read ? 'R' : '-')
      << (write ? 'W' : '-') << " @ 0x" << std::hex << std::uppercase
      << std::setw(4) << std::setfill('0') << addr;
  return out.str();
}

} // namespace Debug
