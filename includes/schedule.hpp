#ifndef SCHEDULE_HPP
#define SCHEDULE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace Debug {
class Debuggable;
class Debugger;
} // namespace Debug
struct runtime_sys_info;

using time_type = std::uint64_t;
using ord_type = std::uint64_t;

/**
 * The outcomes of the handling of a particular event, best explained via an example.
 * Consider the CPU is halted. We can continuously pop events off the queue without
 * worrying about the CPU, and know we need to stop when the components event handler
 * denotes that it fired an interrupt.
 */
enum class ScheduledEventOutcome {
  EVENT_OUTCOME_NONE = 0,      // Default, no synchronization conflict
  EVENT_OUTCOME_INTERRUPT,     // Unpauses CPU after HALT instruction
  EVENT_OUTCOME_VDMA_COMPLETE, // Unpauses CPU after VRAM DMA transfers
};

/**
 * A list of all components that the scheduler uses to determine component IDs. This
 * is necessary so components can easily locate and unmap already scheduled events if
 * they are canceled under certain conditions.
 */
enum class SchedulerComponent : unsigned {
  SCHED_COMPONENT_OAM_DMA = 0,
  SCHED_COMPONENT_VRAM_DMA,
  SCHED_COMPONENT_PPU,
  SCHED_COMPONENT_APU,

  /* For serialization purposes - don't touch */
  SCHED_COMPONENT_COUNT,
};

using event = std::tuple<SchedulerComponent, unsigned>; // (component_id, event_id)
using event_time = std::tuple<time_type, ord_type>;

inline constexpr time_type clks_static_timing(time_type cycles) {
  /**
   * We increment our elapsed clocks counter, technically, at a speed of 8MHz, so that
   * we can align events that run in double speed mode on the odd-cycles.
   */
  return cycles * 2;
}

inline time_type clks_key1_controlled(bool double_speed, time_type cycles) {
  /**
   * Use only if the timing of an event is controlled by KEY1, which is the hardware
   * register used to control the speed switch build into CGB models.
   */
  return cycles * (double_speed ? 1 : 2);
}

class SystemScheduler {
public:
  SystemScheduler(std::optional<Debug::Debugger> &debugger_, runtime_sys_info &sys_);
  ~SystemScheduler();

  // Main system scheduler needs to recover the global event queue
  template <typename T> void parse_savestate(T &t);
  std::optional<time_type> peek_next_cycle() const;
  event pop_next_event();

  time_type schedule_event_in(time_type in_cycles, event e);
  time_type schedule_event_on(time_type cycle, event e);

private:
  struct Implementation;
  std::unique_ptr<Implementation> impl;

  struct runtime_sys_info &sys;
  ord_type ord{};
};

class ChildScheduler {
public:
  using LookupVal = std::optional<time_type>;
  ChildScheduler(SystemScheduler &global_sched, SchedulerComponent component_id,
                 const std::size_t num_events);
  ~ChildScheduler();

  // Child scheduler needs to recover its event lookup vector used for unscheduling. We
  // do not assign these chunk headers, as the structure is rather simple, and should be
  // called by the owning component and serialized as a complex field.
  template <typename T> void parse_savestate(T &t, std::size_t num_events);

  template <typename EventIdType>
  void schedule_event_in(time_type in_cycles, EventIdType event_id) {
    schedule_event_in_impl(in_cycles, static_cast<unsigned>(event_id));
  }

  template <typename EventIdType> void schedule_event_on(time_type cycle, EventIdType event_id) {
    schedule_event_on_impl(cycle, static_cast<unsigned>(event_id));
  }

  template <typename EventIdType> bool unschedule_event(EventIdType event_id) {
    return unschedule_event_impl(static_cast<unsigned>(event_id));
  }

  // Our way of unscheduling events does not actually involve removing entries from the
  // data structures used to implement the scheduler. Instead, we consider stale events
  // dirty and conditionally handle them based on this.
  bool is_event_dirty(time_type scheduled_cycle, unsigned event_id) const;

private:
  std::vector<LookupVal> e_index;

  // Helpers for working with the event tracker. The `e_index` member is a mechanism
  // to know what events have and have not been scheduled for the sake of an efficient
  // event de-scheduling solution.
  void index_put(const unsigned event_id, const time_type t);
  LookupVal index_del(const unsigned event_id);
  void index_load_vec(std::vector<LookupVal> &vec);
  std::vector<LookupVal> index_as_vec() const;

  // Implementations behind previously defined templates which abstract away the
  // original enum type into a generic `unsigned` value.
  void schedule_event_in_impl(time_type in_cycles, unsigned event_id);
  void schedule_event_on_impl(time_type cycle, unsigned event_id);
  bool unschedule_event_impl(unsigned event_id);

  const SchedulerComponent component_id;
  SystemScheduler &g_sched;
};

#endif // SCHEDULE_HPP
