#ifndef GBC_PY_FRONTEND_TESTING_HPP
#define GBC_PY_FRONTEND_TESTING_HPP

#include "frontend/python/wrappers.hpp"

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
[[nodiscard]] bool poll_mooneye_test(PyGameBoyColor &gbc, bool big_step);

#endif // GBC_PY_FRONTEND_TESTING_HPP
