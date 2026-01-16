#ifndef __GBC_H
#define __GBC_H

#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "apu/apu.hpp"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"
#include "timer/timer.hpp"

#include <cstdint>
#include <memory>

class Frontend;

/* A generic data structure that is passed to the components and updated by
 * various MMIO registers that need to know about things like backwards
 * compatability and current operating mode. */
struct runtime_sys_info {
  std::uint64_t elapsed_clocks{};
  bool cgb_mode{};
  bool halted{};

  // For double speed mode, see KEY1 register in `cgb.hpp` for details
  bool speed_switch_armed{};
  bool double_speed{};
};

class GameBoyColor {
public:
  GameBoyColor(Frontend &frontend);
  void insert_cartridge(cart c);
  void init_test_bed();
  void step();

  /* Getters mainly for python bindings */
  AddressBus *get_bus() { return bus.get(); };
  LR35902 *get_cpu() { return cpu.get(); };
  PixelProcessingUnit *get_ppu() { return ppu.get(); }
  TimerUnit *get_timer() { return timer.get(); }

  /* Getters for high level system runtime info */
  bool is_cgb_mode() const { return sys_.cgb_mode; }

private:
  std::unique_ptr<AddressBus> bus{};
  std::unique_ptr<LR35902> cpu{};
  std::unique_ptr<APU> apu{};
  std::unique_ptr<PixelProcessingUnit> ppu{};
  std::unique_ptr<TimerUnit> timer{};

  void step_dma(bool fast_cycle);
  bool vdma_enabled() const;
  void step_processor();

  runtime_sys_info sys_{};
  bool has_cartridge{};
  Frontend &fe_;
};

#endif // __GBC_H
