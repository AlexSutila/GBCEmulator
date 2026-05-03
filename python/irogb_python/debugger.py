from __future__ import annotations

from .gbc_py import BreakReason
from . import gbc_py as core
from typing import Union


class BreakContext:
    def __init__(self, ctx_core: core.BreakContext):
        if not isinstance(ctx_core, core.BreakContext):
            raise ValueError("Expected core.BreakContext")
        self._ctx = ctx_core

    @property
    def reason(self) -> BreakReason:
        return self._ctx.reason

    @property
    def time(self) -> int:
        return self._ctx.time

    @property
    def data(self) -> Union[None, int]:
        return self._ctx.data


class Debugger:
    def __init__(self, debug_core: core.Debugger):
        if not isinstance(debug_core, core.Debugger):
            raise ValueError("Expected core.Debugger")
        self._debug = debug_core

    def breakpoint_add(self, addr: int, reason: BreakReason):
        try:
            self._debug.breakpoint_add(addr, reason)
        except Exception as e:
            raise RuntimeError("Breakpoint addition failed") from e

    def breakpoint_del(self, addr: int):
        try:
            self._debug.breakpoint_del(addr)
        except Exception as e:
            raise RuntimeError("Breakpoint removal failed") from e

    @property
    def breakpoints(self) -> dict:
        return {k: v.to_string() for k, v in self._debug.get_breakpoints().items()}
