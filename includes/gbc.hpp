#ifndef __GBC_H
#define __GBC_H

#include "apu/apu.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "debugger/debugger.hpp"
#include "memory/boot.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/ppu.hpp"
#include "serial/serial.hpp"
#include "timer/timer.hpp"

#include <cstdint>
#include <memory>
#include <optional>

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
  GameBoyColor(Frontend &frontend, const std::string &bios_path);
  GameBoyColor(Frontend &frontend);
  void insert_cartridge(cart c);
  void init_test_bed();
  void step();

  /* Optional debugger configurable by frontend */
  void configure_debugger(Debug::Debugger debugger) {
    debugger_ = std::move(debugger);
  }
  std::optional<Debug::Debugger> &get_debugger() { return debugger_; }

  /* Getters mainly for python bindings */
  AddressBus *get_bus() { return bus.get(); };
  LR35902 *get_cpu() { return cpu.get(); };
  PixelProcessingUnit *get_ppu() { return ppu.get(); }
  TimerUnit *get_timer() { return timer.get(); }
  const runtime_sys_info &get_sys() { return sys_; }

private:
  std::unique_ptr<AddressBus> bus{};
  std::unique_ptr<LR35902> cpu{};
  std::unique_ptr<APU> apu{};
  std::unique_ptr<PixelProcessingUnit> ppu{};
  std::unique_ptr<TimerUnit> timer{};
  std::unique_ptr<SerialUnit> serial{};

  /* Top-level system initialization helpers */
  void system_init(); // Connects all components in the system
  void skip_bios();   // Skips bios when unconfigured

  /* Helpers for initializing emulator state to skip the BIOS */
  void cram_init_mono(IORegisterMapping index, IORegisterMapping data);
  void cram_init_mono();

  /* For moving emulation state along */
  void step_dma(bool fast_cycle);
  bool vdma_enabled() const;
  void step_processor();

  std::optional<Debug::Debugger> debugger_{};
  std::optional<BootROM> bios_{};
  runtime_sys_info sys_{};
  Frontend &fe_;
};

#endif // __GBC_H
