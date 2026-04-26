#include "schedule.hpp"
#include "gbc.hpp"
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>

struct event_sequencer {
  bool operator()(event_time const &a, event_time const &b) const {
    const auto time_a = std::get<0>(a), time_b = std::get<0>(b);
    const auto ord_a = std::get<1>(a), ord_b = std::get<1>(b);
    if (time_a != time_b) [[likely]]
      return time_a > time_b;
    return ord_a > ord_b;
  }
};

/* ======================================================================
 * Global system scheduler implementation
 * ====================================================================== */

// TODO: We can probably use a more efficient data structure for `e_index` since
// we really do not care much about ordering, not as much as mapping at least.
struct SystemScheduler::Implementation {
public:
  ~Implementation() = default;

  std::map<event_time, event, event_sequencer> e_queue;
  std::map<event, event_time> e_index; // Ordering does not matter

  void try_unqueue(event e) {
    if (e_index.empty())
      return;

    auto it = e_index.find(e); // Locate event time
    const event_time &t = it->second;
    e_queue.erase(t);
  }

  void queue(event_time t, event e) {
    e_queue.insert({t, e});
    e_index.insert({e, t});
  }

  event pop() {
    auto it = e_queue.begin();
    event e = it->second;
    e_queue.erase(it);
    e_index.erase(e);
    return e;
  }

  std::optional<time_type> peek() const {
    if (e_queue.empty())
      return std::nullopt;
    const event_time &t = e_queue.begin()->first;
    return std::get<0>(t);
  }
};

SystemScheduler::SystemScheduler(runtime_sys_info &sys_)
    : impl(std::make_unique<SystemScheduler::Implementation>()), // PIMPL
      sys(sys_), ord(0) {}
SystemScheduler::~SystemScheduler() = default;

std::optional<time_type> SystemScheduler::peek_next_cycle() const { return impl->peek(); }
event SystemScheduler::pop_next_event() {
  auto t = peek_next_cycle();
  if (t == std::nullopt) [[unlikely]]
    throw std::runtime_error("Popped an empty queue");

  const event e = impl->pop();
  return e;
}

void SystemScheduler::schedule_event_in(time_type in_cycles, event e) {
  const event_time t = std::make_tuple(sys.elapsed_clocks + in_cycles, ord++);
  impl->queue(t, e);
}

void SystemScheduler::schedule_event_on(time_type cycle, event e) {
  const event_time t = std::make_tuple(cycle, ord++);
  impl->queue(t, e);
}

void SystemScheduler::unschedule_event(event e) const { impl->try_unqueue(e); }

/* ======================================================================
 * Child (per-component) scheduler implementation
 * ====================================================================== */

struct ChildScheduler::Implementation {
public:
  ~Implementation() = default;
};

ChildScheduler::ChildScheduler(SystemScheduler &global_sched, SchedulerComponents component_id)
    : impl(std::make_unique<ChildScheduler::Implementation>()), // PIMPL
      component_id(static_cast<unsigned>(component_id)),        // Should be unsigned enum
      g_sched(global_sched) {}
ChildScheduler::~ChildScheduler() = default;

void ChildScheduler::schedule_event_in_impl(time_type in_cycles, unsigned event_id) const {
  const event e = std::make_tuple(component_id, event_id);
  g_sched.schedule_event_in(in_cycles, e);
}

void ChildScheduler::schedule_event_on_impl(time_type cycle, unsigned event_id) const {
  const event e = std::make_tuple(component_id, event_id);
  g_sched.schedule_event_on(cycle, e);
}

void ChildScheduler::unschedule_event_impl(unsigned event_id) const {
  const event e = std::make_tuple(component_id, event_id);
  g_sched.unschedule_event(e);
}
