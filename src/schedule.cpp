#include "schedule.hpp"
#include "debugger/breakpoint.hpp"
#include "debugger/debugger.hpp"
#include "frontend/logger.hpp"
#include "gbc.hpp"
#include "memory/dma.hpp"
#include "savestate/codec.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <tuple>

struct event_sequencer {
  bool operator()(event_time const &a, event_time const &b) const {
    const auto time_a = std::get<0>(a), time_b = std::get<0>(b);
    const auto ord_a = std::get<1>(a), ord_b = std::get<1>(b);
    if (time_a != time_b) [[likely]]
      return time_a < time_b;
    return ord_a < ord_b;
  }
};

// A custom revision of std::priority_queue that supports random removal
template <typename T> class SchedQueue : public std::priority_queue<T, std::vector<T>> {
public:
  bool remove(const T &val) {
    auto it = std::find(this->c.begin(), this->c.end(), val);
    if (it == this->c.end())
      return false;
    if (it == this->c.begin())
      this->pop();
    else {
      this->erase(it);
      std::make_heap(this->c.begin(), this->c.end(), this->comp);
    }
    return true;
  }
};

/* ======================================================================
 * Convenience configurable debug logging
 * ====================================================================== */
#define SCHED_LOG_ENABLED // WARNING: May clutter logs, leave undefined unless debugging

#ifdef SCHED_LOG_ENABLED
#define LOG_UNQUEUE(message, e, t, sys) sched_log(message, "unqueue", e, t, sys)
#define LOG_QUEUE(message, e, t, sys) sched_log(message, "queue", e, t, sys)
#define LOG_HANDLE(message, e, t, sys) sched_log(message, "handle", e, t, sys)

static inline std::ostream &operator<<(std::ostream &os, const runtime_sys_info &sys) {
  return os << "{ clocks=" << sys.elapsed_clocks << ", vdma=" << sys.vdma_active
            << ", halt=" << sys.halted << ", ds=" << sys.double_speed
            << ", arm=" << sys.speed_switch_armed << ", cgb=" << sys.cgb_mode << " }";
}

static inline void sched_log(const char *message, const char *summary, event e, event_time t,
                             const runtime_sys_info &sys) {
  const auto [comp_id, event_id] = e;
  const auto [time, ord] = t;
  std::ostringstream oss{};

  oss << message << ": (" << static_cast<unsigned>(comp_id) << "," << event_id << ") @ (" << time
      << "," << ord << ") - " << sys;
  Logger::push(LogLevel::Debug, "sched", std::string(summary), oss.str());
}

#else
#define LOG_UNQUEUE(message, e, t, sys)
#define LOG_QUEUE(message, e, t, sys)
#define LOG_HANDLE(message, e, t, sys)
#endif // SCHED_LOG_ENABLED

/* ======================================================================
 * Global system scheduler implementation
 * ====================================================================== */

// TODO: We can probably use a more efficient data structure for `e_index` since
// we really do not care much about ordering, not as much as mapping at least.
struct SystemScheduler::Implementation : public Debug::Debuggable {
public:
  Implementation(std::optional<Debug::Debugger> &debugger_, runtime_sys_info &sys)
      : Debug::Debuggable(debugger_), sys_(sys) {}
  ~Implementation() = default;

  void unqueue(event_time t) { e_queue.erase(t); }

  // We want to note the time at which the event was queued, the handling of
  // the event can be observed through `Debug::BreakReason::BRK_EVENT_POPPED`
  void queue(time_type queued_at, event_time t, event e) {
    try_brk(queued_at, e, Debug::BreakReason::BRK_EVENT_QUEUED);
    LOG_QUEUE("event_queued", e, t, sys_);
    queue(t, e);
  }

  // Pop for event handling
  event pop() {
    auto it = e_queue.begin();
    const auto [t, e] = *it;

    /* Important note, multiple events which were queued up over a wide range
     * of cycle may all be handled on the same cycle, hence we show the time
     * which the event should have been handled. */
    const time_type popped_at = std::get<0>(t);
    try_brk(popped_at, e, Debug::BreakReason::BRK_EVENT_POPPED);
    LOG_HANDLE("event_handled", e, t, sys_);

    e_queue.erase(it);
    return e;
  }

  std::optional<time_type> peek() const {
    if (e_queue.empty())
      return std::nullopt;
    const event_time &t = e_queue.begin()->first;
    return std::get<0>(t);
  }

private:
  void queue(event_time t, event e) { e_queue.insert({t, e}); }

  std::map<event_time, event, event_sequencer> e_queue;
  const runtime_sys_info &sys_;
};

SystemScheduler::SystemScheduler(std::optional<Debug::Debugger> &debugger_, runtime_sys_info &sys_)
    : impl(std::make_unique<SystemScheduler::Implementation>(debugger_, sys_)), // PIMPL
      sys(sys_), ord(0) {}
SystemScheduler::~SystemScheduler() = default;

/**
 * IMPORTANT: Any components which implement a child scheduler that schedules events
 * to the top level system scheduler *must* advertise the total number of events it
 * can queue at any given time here.
 *
 * The overall size of the savestate does not increase unless such events are actually
 * scheduled, only the size of the allocation (libretro constraint).
 */
constexpr std::size_t queue_size_upper_bound() {
  return static_cast<std::size_t>(ObjAttrDMA::SchedulerEvent::EVENT_COUNT) +
         static_cast<std::size_t>(VDMA::SchedulerEvent::EVENT_COUNT);
}

template <typename T> void SystemScheduler::parse_savestate(T &t) {
  constexpr auto version = 1; // Schema revision
  t.chunk_header(version, Savestate::C_SCHEDULER);
  t.eof();
}

template void SystemScheduler::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void SystemScheduler::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void SystemScheduler::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void SystemScheduler::parse_savestate<Savestate::Checker>(Savestate::Checker &);

std::optional<time_type> SystemScheduler::peek_next_cycle() const { return impl->peek(); }
event SystemScheduler::pop_next_event() {
  auto t = peek_next_cycle();
  if (t == std::nullopt) [[unlikely]]
    throw std::runtime_error("Popped an empty queue");

  const event e = impl->pop();
  return e;
}

event_time SystemScheduler::schedule_event_in(time_type in_cycles, event e) {
  const event_time t = std::make_tuple(sys.elapsed_clocks + in_cycles, ord++);
  impl->queue(sys.elapsed_clocks, t, e);
  return t;
}

event_time SystemScheduler::schedule_event_on(time_type cycle, event e) {
  const event_time t = std::make_tuple(cycle, ord++);
  impl->queue(sys.elapsed_clocks, t, e);
  return t;
}

void SystemScheduler::unschedule_event(event_time t) { impl->unqueue(t); }

/* ======================================================================
 * Child (per-component) scheduler implementation
 * ====================================================================== */

struct ChildScheduler::Implementation {
public:
  using LookupVal = std::optional<event_time>;
  Implementation(const std::size_t num_events) : e_index(num_events, std::nullopt) {}

  void put(const unsigned event_id, const event_time t) {
    e_index.at(static_cast<std::size_t>(event_id)) = t;
  }

  LookupVal erase(const unsigned event_id) {
    const std::size_t event_idx = static_cast<std::size_t>(event_id);
    auto ret = e_index.at(event_idx); // Keep copy to remove at top level

    e_index.at(event_idx) = std::nullopt;
    return ret;
  }

private:
  std::vector<LookupVal> e_index;
};

ChildScheduler::ChildScheduler(SystemScheduler &global_sched, SchedulerComponent component_id,
                               const std::size_t num_events)
    : impl(std::make_unique<Implementation>(num_events)), component_id(component_id),
      g_sched(global_sched) {}
ChildScheduler::~ChildScheduler() = default;

void ChildScheduler::schedule_event_in_impl(time_type in_cycles, unsigned event_id) {
  const auto t = g_sched.schedule_event_in(in_cycles, std::make_tuple(component_id, event_id));
  impl->put(event_id, t);
}

void ChildScheduler::schedule_event_on_impl(time_type cycle, unsigned event_id) {
  const auto t = g_sched.schedule_event_on(cycle, std::make_tuple(component_id, event_id));
  impl->put(event_id, t);
}

bool ChildScheduler::unschedule_event_impl(unsigned event_id) {
  const auto t = impl->erase(event_id);
  if (!t.has_value())
    return false;

  g_sched.unschedule_event(t.value());
  return true;
}
