#ifndef GBC_HPP
#define GBC_HPP

#include "apu/apu.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "debugger/debugger.hpp"
#include "memory/boot.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/ppu.hpp"
#include "serial.hpp"
#include "timer.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

class GameBoyColor final : Debug::Debuggable {
public:
  enum CheatFormat : int {
    CHEAT_AUTO = 0,
    CHEAT_GAMESHARK = 1,
    CHEAT_GAME_GENIE = 2,
    CHEAT_RAW = 3,
    CHEAT_CODEBREAKER = 4,
  };
  struct CheatCode {
    bool enabled{true};
    std::string code;
    int format{CHEAT_AUTO};
  };
  struct CheatStats {
    std::size_t total{};
    std::size_t enabled{};
    std::size_t active{};
    std::size_t rejected{};
  };

  GameBoyColor(Frontend &frontend, const std::string &bios_path);
  GameBoyColor(Frontend &frontend, const BootROM &rom);
  explicit GameBoyColor(Frontend &frontend);
  void insert_cartridge(const cart& c);
  void init_test_bed() const;
  void step();
  CheatStats configure_cheats(const std::vector<CheatCode>& cheats);
  [[nodiscard]] CheatStats get_cheat_stats() const { return cheat_stats_; }
  [[nodiscard]] bool savestate_ready() const;
  [[nodiscard]] std::vector<byte_t> serialize_savestate() const;
  void deserialize_savestate(std::span<const byte_t> data);

  /* Optional debugger configurable by frontend */
  void configure_debugger(Debug::Debugger debugger) {
    debugger_ = std::move(debugger);
  }
  std::optional<Debug::Debugger> &get_debugger() { return debugger_; }

  /* Getters mainly for python bindings */
  [[nodiscard]] AddressBus *get_bus() const { return bus.get(); };
  [[nodiscard]] LR35902 *get_cpu() const { return cpu.get(); };
  [[nodiscard]] PixelProcessingUnit *get_ppu() const { return ppu.get(); }
  [[nodiscard]] TimerUnit *get_timer() const { return timer.get(); }
  [[nodiscard]] const runtime_sys_info &get_sys() const { return sys_; }

private:
  std::unique_ptr<AddressBus> bus{};
  std::unique_ptr<LR35902> cpu{};
  std::unique_ptr<APU> apu{};
  std::unique_ptr<PixelProcessingUnit> ppu{};
  std::unique_ptr<TimerUnit> timer{};
  std::unique_ptr<SerialUnit> serial{};

  /* Top-level system initialization helpers */
  void system_init(); // Connects all components in the system
  void skip_bios() const;   // Skips bios when unconfigured

  /* Helpers for initializing emulator state to skip the BIOS */
  void cram_init_mono(IORegisterMapping index, IORegisterMapping data) const;
  void cram_init_mono() const;

  /* For moving emulation state along */
  void step_dma(bool fast_cycle) const;
  [[nodiscard]] bool vdma_enabled() const;
  void step_processor() const;
  CheatStats cheat_stats_{};

  std::optional<Debug::Debugger> debugger_{};
  std::optional<BootROM> bios_{};
  runtime_sys_info sys_{};
  Frontend &fe_;
};

#endif // GBC_HPP
