from __future__ import annotations

from . import gbc_py as core


class ProcessorState:
    def __init__(self, cpu_state: core.ProcessorState):
        if not isinstance(cpu_state, core.ProcessorState):
            raise ValueError("Expected core.ProcessorState")
        self._cpu_state = cpu_state

    @property
    def pc(self) -> int:
        return self._cpu_state.pc

    @property
    def sp(self) -> int:
        return self._cpu_state.sp

    # 8-bit registers
    @property
    def a(self) -> int:
        return self._cpu_state.a

    @property
    def b(self) -> int:
        return self._cpu_state.b

    @property
    def c(self) -> int:
        return self._cpu_state.c

    @property
    def d(self) -> int:
        return self._cpu_state.d

    @property
    def e(self) -> int:
        return self._cpu_state.e

    @property
    def f(self) -> int:
        return self._cpu_state.f

    @property
    def h(self) -> int:
        return self._cpu_state.h

    @property
    def l(self) -> int:  # Linter is whining but idgaf
        return self._cpu_state.l

    @property
    def ime_enabled(self) -> bool:
        return self._cpu_state.ime_enabled

    def __repr__(self) -> str:
        return (
            f"PC={self.pc:04X} SP={self.sp:04X}  "
            f"A={self.a:02X} F={self.f:02X}  "
            f"B={self.b:02X} C={self.c:02X}  "
            f"D={self.d:02X} E={self.e:02X}  "
            f"H={self.h:02X} L={self.l:02X}  "
            f"IME={'1' if self.ime_enabled else '0'}"
        )
