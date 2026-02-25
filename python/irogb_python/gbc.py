from __future__ import annotations

from typing import Callable

from .cart import Cartridge
from . import gbc_py as core


class GameBoyColor:
    def __init__(self, cartridge: Cartridge = None, break_cb: Callable = None):
        try:
            self._gbc = (
                core.GameBoyColor(break_cb)
                if break_cb is not None
                else core.GameBoyColor()
            )
        except Exception as e:
            raise RuntimeError(
                "Failed to initialize GameBoyColor core instance"
            ) from e

        # Entirely optional, only check if provided
        if cartridge is not None:
            self.insert_cartridge(cartridge)

    def insert_cartridge(self, cartridge: Cartridge):
        self._gbc.insert_cartridge(cartridge._raw)

    def step(self, cycles: int = None):
        if cycles is not None:
            self._gbc.step_cycles(cycles)
        else:  # Latent, not recommended
            self._gbc.step()
