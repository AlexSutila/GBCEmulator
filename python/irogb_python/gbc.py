from __future__ import annotations

from typing import Callable

from .cpu import ProcessorState
from .ppu import PPUState, RenderedFrame
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

    '''

    Internal helpers for argument correctness checks

    '''

    def _check_bitwidth_byte(self, value: int):
        if value > 0xFF or value < 0x00:
            raise ValueError("`value` must be 8-bits")

    def _check_bitwidth_addr(self, addr: int):
        if addr > 0xFFFF or addr < 0x0000:
            raise ValueError("`addr` must be 16-bits")

    '''

    Publicly exposed interface implementation

    '''

    @property
    def cpu_state(self):
        try:
            cpu = self._gbc.get_cpu()
        except Exception as e:
            raise RuntimeError(
                "Failed to initialize ProcessorState core instance"
            ) from e
        return ProcessorState(cpu.get_state())

    @property
    def ppu_state(self):
        try:
            ppu = self._gbc.get_ppu()
        except Exception as e:
            raise RuntimeError(
                "Failed to initialize PPUState core instance"
            ) from e
        return PPUState(ppu.get_state())

    @property
    def frame(self):
        try:
            frame = self._gbc.get_frame()
        except Exception as e:
            raise RuntimeError(
                "Failed to acquire frame from core"
            ) from e
        return RenderedFrame(frame)

    def insert_cartridge(self, cartridge: Cartridge):
        self._gbc.insert_cartridge(cartridge._raw)

    def read_byte(self, addr: int):
        self._check_bitwidth_addr(addr)
        addr_bus = self._gbc.get_bus()
        return addr_bus.read_byte(addr)

    def write_byte(self, addr: int, value: int):
        self._check_bitwidth_addr(addr)
        self._check_bitwidth_byte(value)
        addr_bus = self._gbc.get_bus()
        return addr_bus.write_byte(addr, value)

    def step_frame(self):
        cycles = 70224  # One frame worth of t-cycles
        self.step(cycles=cycles)

    def step_scanline(self):
        cycles = 456  # One scanline worth of t-cycles
        self.step(cycles=cycles)

    def step(self, cycles: int = None):
        if cycles is not None:
            self._gbc.step_cycles(cycles)
        else:  # Latent, not recommended
            self._gbc.step()
