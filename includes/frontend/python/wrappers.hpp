#ifndef __PY_FRONTEND_WRAPPERS_H
#define __PY_FRONTEND_WRAPPERS_H

#include "frontend/python/frontend.hpp"
#include <array>
#include <cstdint>

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
  PixelProcessingUnit *get_ppu();
  AddressBus *get_bus();
  TimerUnit *get_timer();
  LR35902 *get_cpu();

  /**
   * Modify button input state. Lots of possibilities with this regarding tool
   * assisted speedrun/speedplay automation.
   */
  void put_joyp_state(std::uint8_t state);

private:
  PyFrontend fe_;
};

#endif // __PY_FRONTEND_WRAPPERS_H
