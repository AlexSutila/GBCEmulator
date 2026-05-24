#include "schedule.hpp"
#include "debugger/breakpoint.hpp"
#include "debugger/debugger.hpp"
#include "gbc.hpp"
#include "memory/dma.hpp"
#include "ppu/ppu.hpp"
#include "savestate/codec.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <tuple>

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
         static_cast<std::size_t>(VDMA::SchedulerEvent::EVENT_COUNT) +
         static_cast<std::size_t>(PixelProcessingUnit::SchedulerEvent::EVENT_COUNT) +
         static_cast<std::size_t>(APU::SchedulerEvent::EVENT_COUNT);
}

struct SchedNode {
  event_time t;
  event e;

  // Order is flipped intentionally so that sooner events are prioritized.
  bool operator<(const SchedNode &other) const { return t > other.t; }
  bool operator==(const SchedNode &other) const { return t == other.t; }
};

// A custom revision of std::priority_queue that supports random removal and avoids heap
// reallocations and data copies during pushes to the underlying vector data structure
class SchedQueue : public std::priority_queue<SchedNode, std::vector<SchedNode>> {
public:
  explicit SchedQueue() {
    constexpr auto total_events = queue_size_upper_bound();
    this->c.reserve(total_events);
  }

  void remove(event_time t) {
    auto &vec = this->c;

    auto it = std::find_if(vec.begin(), vec.end(), [&](const SchedNode &n) { return n.t == t; });
    if (it == vec.end())
      return;
    vec.erase(it);

    // Need to restructure the heap after removal
    std::make_heap(vec.begin(), vec.end(), this->comp);
  }

  void load_vec(std::vector<SchedNode> vec) {
    this->c = std::move(vec);
    std::make_heap(this->c.begin(), this->c.end(), this->comp);
  }

  std::vector<SchedNode> as_vec() const { return this->c; }
};

/* ======================================================================
 * Global system scheduler implementation
 * ====================================================================== */

struct SystemScheduler::Implementation : public Debug::Debuggable {
public:
  Implementation(std::optional<Debug::Debugger> &debugger_) : Debug::Debuggable(debugger_) {}
  ~Implementation() = default;

  // If this was invoked, it makes an implicit assumption that the e_index for the
  // corresponding child scheduler had its entry populated for this event. If that
  // entry is not populated, this should never be invoked.
  void unqueue(event_time t) { e_queue.remove(t); }

  // We want to note the time at which the event was queued, the handling of
  // the event can be observed through `Debug::BreakReason::BRK_EVENT_POPPED`
  void queue(time_type queued_at, event_time t, event e) {
    try_brk(queued_at, e, Debug::BreakReason::BRK_EVENT_QUEUED);
    e_queue.emplace(SchedNode{t, e});
  }

  // Pop for event handling
  event pop() {
    const auto [t, e] = e_queue.top();
    try_brk(std::get<0>(t), e, Debug::BreakReason::BRK_EVENT_POPPED);

    e_queue.pop();
    return e;
  }

  // Peek at the next event for determining when it is necessary to begin
  // popping events off the global system event queue
  std::optional<time_type> peek() const {
    if (e_queue.empty())
      return std::nullopt;

    auto [t, e] = e_queue.top();
    return std::get<0>(t);
  }

  // For serialization / deserialization
  void load_vec(std::vector<SchedNode> &vec) { e_queue.load_vec(vec); }
  std::vector<SchedNode> as_vec() const { return e_queue.as_vec(); }

private:
  SchedQueue e_queue;
};

template <typename T> void SystemScheduler::parse_savestate(T &t) {
  constexpr auto version = 2; // Schema revision
  enum : std::uint16_t {
    F_EVENT_QUEUE,
    F_ORD,
  };
  t.chunk_header(version, Savestate::C_SYS_SCHEDULER);

  constexpr auto max_size = queue_size_upper_bound();
  std::vector<SchedNode> event_vec{}; // Temporary queue state representation

  // If we are writing, use existing state
  if (t.op() == Savestate::OP_WRITE)
    event_vec = impl->as_vec();

  // Serialize event queue under upper bound queue length assumption
  t.field_vector(F_EVENT_QUEUE, event_vec, max_size, [&](T &t, auto &s) {
    // Careful, use refs here so that the state is properly restored during load state.
    auto &[comp_id, event_id] = s.e;
    auto &[time, ord] = s.t;

    // Write fields directly rather than serializing a struct so we dont deal with wasted
    // bytes caused by padding. Be careful, this must lie with `max_bytes`.
    t.field_generic(0, comp_id);
    t.field_generic(1, event_id);
    t.field_generic(2, time);
    t.field_generic(3, ord);
  });

  // If we are reading, use new state. Make sure stale metadata is cleared.
  if (t.op() == Savestate::OP_READ)
    impl->load_vec(event_vec);

  t.field_generic(F_ORD, ord);
  t.eof();
}

template void SystemScheduler::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void SystemScheduler::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void SystemScheduler::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void SystemScheduler::parse_savestate<Savestate::Checker>(Savestate::Checker &);

SystemScheduler::SystemScheduler(std::optional<Debug::Debugger> &debugger_, runtime_sys_info &sys_)
    : impl(std::make_unique<SystemScheduler::Implementation>(debugger_)), // PIMPL
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

bool SystemScheduler::unschedule_event(event_time t) {
  auto [time, ord] = t;
  if (time < sys.elapsed_clocks) {
    // It is too late to unschedule this event. We leave the stale entry because its
    // more efficient to do so, rather than needing a reverse mapping in the system
    // scheduler to remove it.
    return false;
  }

  impl->unqueue(t);
  return true;
}

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

  // For serialization / deserialization
  void load_vec(std::vector<LookupVal> &vec) { e_index = std::move(vec); }
  std::vector<LookupVal> as_vec() const { return e_index; }

private:
  std::vector<LookupVal> e_index;
};

// We pass in the size so we can garuntee the sizes of the vectors match
template <typename T> void ChildScheduler::parse_savestate(T &t, std::size_t num_events) {
  std::vector<Implementation::LookupVal> vec(num_events, std::nullopt);
  constexpr auto version = 1; // Schema revision
  enum : std::uint16_t { F_TIME, F_ORD };
  t.chunk_header(version, Savestate::C_CHILD_SCHEDULER);

  // If we are writing, use existing state
  if (t.op() == Savestate::OP_WRITE) {
    vec = impl->as_vec(); // Should always match num_events
    assert(vec.size() == num_events);
  }

  // The indices should align with the components event ID enum values
  for (std::size_t event_id{0}; event_id < num_events; ++event_id) {
    t.field_optional(event_id, vec.at(event_id), [&](T &t, auto &s) {
      auto &[time, ord] = s;
      t.field_generic(F_TIME, time);
      t.field_generic(F_ORD, ord);
    });
  }

  // If we are reading, use new state. Make sure stale metadata is cleared.
  if (t.op() == Savestate::OP_READ) {
    assert(vec.size() == num_events);
    impl->load_vec(vec);
  }

  t.eof();
}

template void ChildScheduler::parse_savestate<Savestate::Writer>(Savestate::Writer &, std::size_t);
template void ChildScheduler::parse_savestate<Savestate::Reader>(Savestate::Reader &, std::size_t);
template void ChildScheduler::parse_savestate<Savestate::Sizer>(Savestate::Sizer &, std::size_t);
template void ChildScheduler::parse_savestate<Savestate::Checker>(Savestate::Checker &,
                                                                  std::size_t);

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
  return g_sched.unschedule_event(t.value());
}
