from __future__ import annotations

from ..cpu import ProcessorState
from ..gbc import GameBoyColor
from ..cart import Cartridge

from .. import gbc_py as core


def run_mooneye_test(cartridge: Cartridge):
    gbc = GameBoyColor(cartridge=cartridge)

    # Invoke the cpp backend to do this for speed sake
    if not core.poll_mooneye_test(gbc._gbc):
        return None  # Took too long
    return gbc.cpu_state


def eval_mooneye_cpu_state(cpu_state: ProcessorState):
    assert cpu_state.b == 3
    assert cpu_state.c == 5
    assert cpu_state.d == 8
    assert cpu_state.e == 13
    assert cpu_state.h == 21
    assert cpu_state.l == 34
