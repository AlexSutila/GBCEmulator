#ifndef GBC_PY_FRONTEND_WRAPPERS_HPP
#define GBC_PY_FRONTEND_WRAPPERS_HPP

#include "debugger/breakpoint.hpp"
#include "debugger/debugger.hpp"
#include "emu_types.hpp"
#include "frontend/python/frontend.hpp"
#include <array>
#include <functional>
#include <optional>
#include <pybind11/pybind11.h>

class PixelProcessingUnit;
class AddressBus;
class TimerUnit;
class LR35902;
struct cart;

class PyGameBoyColor {
  using frame_buf_t = std::array<std::uint32_t, 160 * 144>;

public:
  PyGameBoyColor();
  void insert_cartridge(cart c);
  frame_buf_t get_frame();

  /**
   * This constructor configures the debugger and allows for python callbacks to
   * be invoked upon being hit.
   */
  explicit PyGameBoyColor(const pybind11::function& callback);
  void breakpoint_add(addr_t addr, Debug::BreakReason reason);
  void breakpoint_del(addr_t addr);

  /**
   * Emulation driver methods
   */
  void step_cycles(int cycles);
  void step();

  /**
   * Initializes a hypothetical cartridge with no MBC circuitry, which is purely
   * RAM. Allows for easy experimentation with hardware and poking random bytes.
   */
  void init_test_bed();

  /**
   * Getters exposing individual hardware components. Just because its fun, we
   * intentionally allow you to alter the state of these components arbitrarily.
   */
  std::optional<Debug::Debugger> &get_debugger();
  PixelProcessingUnit *get_ppu();
  AddressBus *get_bus();
  TimerUnit *get_timer();
  LR35902 *get_cpu();

  /**
   * Modify button input state. Lots of possibilities with this regarding tool
   * assisted speedrun/speed-play automation.
   */
  void put_joyp_state(std::uint8_t state);

private:
  std::function<Debug::BreakReason()> cb_;
  PyFrontend fe_;
};

#endif // GBC_PY_FRONTEND_WRAPPERS_HPP
