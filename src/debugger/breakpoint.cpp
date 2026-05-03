#include "debugger/breakpoint.hpp"
#include "emu_types.hpp"
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace Debug {

Breakpoint::Breakpoint(const BreakReason reason_flags, const addr_t watch_addr)
    : context(watch_addr), reasons(reason_flags) // Should be read/write/execute only
{
  constexpr auto BRK_ALLOWED_FLAGS = BRK_ADDRESS_EXECUTED | BRK_ADDRESS_READ | BRK_ADDRESS_WRITTEN;
  if ((reason_flags & ~BRK_ALLOWED_FLAGS) != 0)
    throw std::runtime_error("Breakpoint::Breakpoint() bad flags");
}

Breakpoint::Breakpoint(const BreakReason reason_flags, const event e)
    : context(e), reasons(reason_flags) // Should be read/write/execute only
{
  constexpr auto BRK_ALLOWED_FLAGS = BRK_EVENT_QUEUED; // TODO: May include some for handling?
  if ((reason_flags & ~BRK_ALLOWED_FLAGS) != 0)
    throw std::runtime_error("Breakpoint::Breakpoint() bad flags");
}

bool Breakpoint::eval(const BreakReason reason_flags) const {
  return (reason_flags & reasons) != 0;
}

bool Breakpoint::has_flag(const BreakReason flag) const { return (flag & reasons) != 0; }

[[nodiscard]] std::string Breakpoint::to_string() const {
  const auto r = static_cast<unsigned>(reasons);

  if (std::holds_alternative<addr_t>(context); addr_t addr = std::get<addr_t>(context)) {
    const bool exec = r & BRK_ADDRESS_EXECUTED;
    const bool read = r & BRK_ADDRESS_READ;
    const bool write = r & BRK_ADDRESS_WRITTEN;

    std::ostringstream out;
    out << "BP " << (exec ? 'E' : '-') << (read ? 'R' : '-') << (write ? 'W' : '-') << " @ 0x"
        << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
        << static_cast<unsigned>(addr);
    return out.str();
  }

  if (std::holds_alternative<event>(context)) {
    const auto [comp_id, event_id] = std::get<event>(context);

    std::ostringstream out;
    out << "BP [event-queued] @ (" << static_cast<unsigned>(comp_id) << ":" << event_id << ")";
    return out.str();
  }

  return "BP";
}

} // namespace Debug
