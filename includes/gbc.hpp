#ifndef __GBC_H
#define __GBC_H

#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "frontend/renderer.hpp"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"

#include <cstddef>
#include <memory>

class GameBoyColor {
public:
  GameBoyColor(bool headless);
  void insert_cartridge(cart c);
  void init_test_bed();
  void run();

  /* Getters mainly for python bindings */
  AddressBus *get_bus() { return bus.get(); };
  LR35902 *get_cpu() { return cpu.get(); };
  PixelProcessor *get_ppu() { return ppu.get(); }
  Timer::TimerUnit *get_timer() { return timer.get(); }

private:
  std::unique_ptr<Renderer> renderer{};
  std::unique_ptr<AddressBus> bus{};
  std::unique_ptr<LR35902> cpu{};
  std::unique_ptr<PixelProcessor> ppu{};
  std::size_t elapsed_clocks_{};
  std::unique_ptr<Timer::TimerUnit> timer{};
};

#endif // __GBC_H
