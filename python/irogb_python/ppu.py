from __future__ import annotations

import matplotlib.pyplot as plt
import numpy as np

from . import gbc_py as core


class RenderedFrame:
    def __init__(self, pixel_data: np.ndarray):
        pixels_u32 = np.asarray(pixel_data, dtype=np.uint32)
        pixels_u8 = pixels_u32.view(np.uint8).reshape((144, 160, 4))
        self._pixel_data = pixels_u8[..., [2, 1, 0]]  # ARGB8888 → RGB

    def as_numpy(self) -> np.ndarray:
        return self._pixel_data

    def show(self):
        plt.figure()
        plt.imshow(self._pixel_data)
        plt.show()


class PPUState:
    def __init__(self, ppu_state: core.PPUState):
        if not isinstance(ppu_state, core.PPUState):
            raise ValueError("Expected core.PPUState")
        self._ppu_state = ppu_state

    @property
    def state(self):
        return self._ppu_state.state

    @property
    def lcdc(self):
        return self._ppu_state.lcdc

    @property
    def stat(self):
        return self._ppu_state.stat

    @property
    def scx(self):
        return self._ppu_state.scx

    @property
    def scy(self):
        return self._ppu_state.scy

    @property
    def wx(self):
        return self._ppu_state.wx

    @property
    def wy(self):
        return self._ppu_state.wy

    @property
    def lyc(self):
        return self._ppu_state.lyc

    @property
    def ly(self):
        return self._ppu_state.ly

    def __repr__(self):
        return (
            f"PixelProcessorState("
            f"state={self.state}, "
            f"lcdc=0x{self.lcdc:02X}, "
            f"stat=0x{self.stat:02X}, "
            f"scx={self.scx}, "
            f"scy={self.scy}, "
            f"wx={self.wx}, "
            f"wy={self.wy}, "
            f"lyc={self.lyc}, "
            f"ly={self.ly}"
            f")"
        )
