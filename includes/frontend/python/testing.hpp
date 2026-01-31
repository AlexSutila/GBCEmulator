#ifndef __PY_FRONTEND_TESTING_H
#define __PY_FRONTEND_TESTING_H

#include "frontend/python/wrappers.hpp"
#include <cstdint>

/**
 * For evaluating test roms from the mooneye test suite. The workflow here,
 would * be to invoke this and evaluate the CPU registers once it returns. If it
 returns * false it never hit the `breakpoint` instruction the test suite uses.
 * ------------------------------------------------------------------------------
 * Pythonic code to do this would look something like what is shown below:
    ; assert poll_mooneye_test(gbc), 'Took too long'
 *  ; state = gbc.get_cpu().get_state()
 *  ; assert state.b == 3
 *  ; assert state.c == 5
 *  ; assert state.d == 8
 *  ; assert state.e == 13
 *  ; assert state.h == 21
 *  ; assert state.l == 34
 */
[[nodiscard]] inline bool poll_mooneye_test(PyGameBoyColor &gbc) {
  std::uint32_t max_cycles = 10000000, cycles = 0;
  byte_t op = 0;
  while (op != 0x40 && cycles < max_cycles) {
    const auto &bus = gbc.get_bus();
    const auto &cpu = gbc.get_cpu();

    const auto state = cpu->get_state();
    op = bus->read_byte(state.pc);

    gbc.step(); // Run until 'LD B, B'
    gbc.step();
    gbc.step();
    gbc.step();
    ++cycles;
  }
  return op == 0x40;
}

#endif // __PY_FRONTEND_TESTING_H
