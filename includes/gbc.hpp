#ifndef __GBC_H
#define __GBC_H

#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "frontend/renderer.hpp"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"
#include "timer/timer.hpp"

#include <cstdint>
#include <memory>

/* A generic data structure that is passed to the components and updated by
 * various MMIO registers that need to know about things like backwards
 * compatability and current operating mode. */
struct runtime_sys_info {
  bool double_speed{};
  bool cgb_mode{};
  std::uint64_t elapsed_clocks{};
};

class GameBoyColor {
public:
  GameBoyColor(bool headless);
  void insert_cartridge(cart c);
  void init_test_bed();
  void step();
  void run();

  /* Getters mainly for python bindings */
  AddressBus *get_bus() { return bus.get(); };
  LR35902 *get_cpu() { return cpu.get(); };
  PixelProcessingUnit *get_ppu() { return ppu.get(); }
  TimerUnit *get_timer() { return timer.get(); }

private:
  std::unique_ptr<Renderer> renderer{};
  std::unique_ptr<AddressBus> bus{};
  std::unique_ptr<LR35902> cpu{};
  std::unique_ptr<PixelProcessingUnit> ppu{};
  std::unique_ptr<TimerUnit> timer{};
  runtime_sys_info sys;
  bool has_cartridge{};
};

#endif // __GBC_H
