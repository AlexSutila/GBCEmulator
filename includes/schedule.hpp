#ifndef SCHEDULE_HPP
#define SCHEDULE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>

struct runtime_sys_info;

using time_type = std::uint64_t;
using ord_type = std::uint64_t;

enum SchedulerComponents : unsigned {
  SCHED_COMPONENT_OAM_DMA = 0,

  /* For serialization purposes - don't touch */
  SCHED_COMPONENT_COUNT,
};

using event = std::tuple<SchedulerComponents, unsigned>; // (component_id, event_id)
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
  SystemScheduler(runtime_sys_info &sys);
  ~SystemScheduler();

  // Serialization gets hairy, see implementation file for details
  template <typename T> void parse_savestate(T &t);
  std::optional<time_type> peek_next_cycle() const;
  event pop_next_event();

  void schedule_event_in(time_type in_cycles, event e);
  void schedule_event_on(time_type cycle, event e);
  void unschedule_event(event e) const;

private:
  struct Implementation;
  std::unique_ptr<Implementation> impl;

  struct runtime_sys_info &sys;
  ord_type ord{};
};

class ChildScheduler {
public:
  ChildScheduler(SystemScheduler &global_sched, SchedulerComponents component_id);
  ~ChildScheduler();

  template <typename EventIdType>
  void schedule_event_in(time_type in_cycles, EventIdType event_id) const {
    schedule_event_in_impl(in_cycles, static_cast<unsigned>(event_id));
  }

  template <typename EventIdType>
  void schedule_event_on(time_type cycle, EventIdType event_id) const {
    schedule_event_on_impl(cycle, static_cast<unsigned>(event_id));
  }

  template <typename EventIdType> void unschedule_event(EventIdType event_id) const {
    unschedule_event_impl(static_cast<unsigned>(event_id));
  }

private:
  void schedule_event_in_impl(time_type in_cycles, unsigned event_id) const;
  void schedule_event_on_impl(time_type cycle, unsigned event_id) const;
  void unschedule_event_impl(unsigned event_id) const;

  const SchedulerComponents component_id;
  SystemScheduler &g_sched;
};

#endif // SCHEDULE_HPP
