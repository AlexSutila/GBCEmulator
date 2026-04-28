#include "schedule.hpp"
#include "gbc.hpp"
#include "memory/dma.hpp"
#include "savestate/codec.hpp"
#include <cstdint>
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

  /* ======================================================================
   * Scheduler serialization details: (((nightmare)))
   * ----------------------------------------------------------------------
   * There are numerous problems we had to solve here because LIBRETRO does
   * not like it when the size of your savestates grows during runtime. So,
   * to do this, we intentionally introduce a limitation that requires that
   * each event, across all components, is only scheduled once.
   *
   * By doing this, we can enforce an upper bound on the amount of memory
   * needed for serializing the scheduler. We will save and load each event
   * to and from a vector, since the pairs of componend IDs and event IDs
   * should all be unique.
   *
   * Because of how we wrote our serialization framework, we should be able
   * to load a dynamic number of fields to and from a vector, and use it to
   * save and restore the state of the scheduler.
   * ====================================================================== */
  struct SavedScheduledEvent {
    SchedulerComponents comp_id;
    unsigned event_id;
    time_type event_time;
    ord_type event_ord;
  };

  std::vector<SavedScheduledEvent> as_vec() const {
    std::vector<SavedScheduledEvent> ret{};

    // The data in `e_queue` and `e_index` is conveniently redundant, so
    // we only have to iterate one of them for serialization purposes.
    for (auto [t, e] : e_queue) {
      ret.push_back({
          .comp_id = std::get<0>(e),
          .event_id = std::get<1>(e),
          .event_time = std::get<0>(t),
          .event_ord = std::get<1>(t),
      });
    }
    return ret;
  }

  void from_vec(std::vector<SavedScheduledEvent> &vec) {
    for (auto s : vec) {
      const event_time t = {s.event_time, s.event_ord};
      const event e = {s.comp_id, s.event_id};
      e_queue.clear();
      e_index.clear();

      // Restoring is more compex because we have to restore the mapping
      // and the reverse mapping for seamless scheduling and unscheduling.
      queue(t, e);
    }
  }
};

SystemScheduler::SystemScheduler(runtime_sys_info &sys_)
    : impl(std::make_unique<SystemScheduler::Implementation>()), // PIMPL
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
  return static_cast<std::size_t>(ObjAttrDMA::SchedulerEvents::EVENT_COUNT) +
         static_cast<std::size_t>(VDMA::SchedulerEvents::EVENT_COUNT);
}

template <typename T> void SystemScheduler::parse_savestate(T &t) {
  using queue_entry = Implementation::SavedScheduledEvent;
  constexpr auto version = 1; // Schema revision
  enum : std::uint16_t {
    F_EVENT_QUEUE = 1,
    F_ORD,
  };
  t.chunk_header(version, Savestate::C_SCHEDULER);

  constexpr auto max_bytes = sizeof(queue_entry) * queue_size_upper_bound();
  std::vector<queue_entry> event_vec{};

  // If we are writing, use existing state
  if (t.op() == Savestate::OP_WRITE)
    event_vec = impl->as_vec();

  // Serialize event queue under upper bound queue length assumption
  t.field_vector(F_EVENT_QUEUE, event_vec, max_bytes, [&](T &t, auto &s) {
    t.field_generic(0, s.comp_id);
    t.field_generic(1, s.event_id);
    t.field_generic(2, s.event_time);
    t.field_generic(3, s.event_ord);
  });

  // If we are reading, use new state. Make sure stale metadata is cleared.
  if (t.op() == Savestate::OP_READ)
    impl->from_vec(event_vec);

  t.field_generic(F_ORD, ord);
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

ChildScheduler::ChildScheduler(SystemScheduler &global_sched, SchedulerComponents component_id)
    : component_id(component_id), g_sched(global_sched) {}
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
