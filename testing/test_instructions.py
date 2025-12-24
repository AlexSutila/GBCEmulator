#!/usr/bin/env python3
from gbc_py import (
    GameBoyColor,
    ProcessorState,
)
from pydantic import BaseModel, ValidationError
from typing import List, Optional, Tuple
import requests
import pytest

BASEURL = 'https://raw.githubusercontent.com/SingleStepTests/sm83'
CycleEntry = Tuple[int, Optional[int], str]
RamEntry = Tuple[int, int]


class CPUState(BaseModel):
    pc: int
    sp: int
    a: int
    b: int
    c: int
    d: int
    e: int
    f: int
    h: int
    l: int
    ime: int
    ram: List[RamEntry]


class CpuTestVector(BaseModel):
    name: str
    initial: CPUState
    final: CPUState
    cycles: List[CycleEntry]


def load_test_vectors(url: str) -> List[CpuTestVector]:
    try:
        resp = requests.get(url, timeout=10)
        resp.raise_for_status()
        data = resp.json()
        return [CpuTestVector.model_validate(item) for item in data]

    except requests.RequestException as e:
        raise RuntimeError(f"HTTP error loading test vectors: {e}") from e
    except ValidationError as e:
        raise RuntimeError(f"Invalid test vector schema: {e}") from e


def __get_opcodes() -> List[str]:
    exclude = [
        "10",  # Stop
        "76",  # Halt
        "cb",  # CB prefix instructions
        "d3",  # Illegal
        "db",  # Illegal
        "dd",  # Illegal
        "e3",  # Illegal
        "e4",  # Illegal
        "eb",  # Illegal
        "ec",  # Illegal
        "ed",  # Illegal
        "f4",  # Illegal
        "fc",  # Illegal
        "fd",  # Illegal
    ]
    return [f"{i:02x}" for i in range(0x100) if f"{i:02x}" not in exclude]


def __get_cb_opcodes() -> List[str]:
    return [f"{i:02x}" for i in range(0x100)]


def __evaluate_test_vectors(test_vecs: List[CpuTestVector]):
    for test_vec in test_vecs:
        skip, no_steps = False, 0

        # Emulator instance
        gbc = GameBoyColor()
        gbc.init_test_bed()
        cpu = gbc.get_cpu()

        # Get bus reference and disable boot ROM
        bus = gbc.get_bus()
        bus.write_byte(0xFF50, 0)

        # Update RAM state - skip a couple address ranges because the test
        # suite sucks miserable ass and doesn't account for their behavior
        for addr, byte in test_vec.initial.ram:
            if addr >= 0xFF00 and addr <= 0xFF7F:
                skip = True  # Skip IO-registers
            elif addr >= 0xFEA0 and addr <= 0xFEFF:
                skip = True  # Skip unusable memory
            elif addr >= 0xE000 and addr <= 0xFDFF:
                skip = True  # Skip echo RAM
            else:
                bus.write_byte(addr, byte)

        # For the time being, I am skipping bizzare cases that rely on accurate
        # emulation of MMIO registers. This test is supposed to be rudimentary
        # and only evaluate CPU accuracy. Volatile memory locations and a good
        # ISA replication are unrelated.
        if skip:
            continue

        # Load initial state
        state = ProcessorState()
        state.pc = test_vec.initial.pc
        state.sp = test_vec.initial.sp
        state.a = test_vec.initial.a
        state.b = test_vec.initial.b
        state.c = test_vec.initial.c
        state.d = test_vec.initial.d
        state.e = test_vec.initial.e
        state.h = test_vec.initial.h
        state.l = test_vec.initial.l
        state.f = test_vec.initial.f
        state.ime_enabled = test_vec.initial.ime
        cpu.load_state(state)

        # Execute instructions
        while cpu.get_state().pc != test_vec.final.pc:
            no_steps = no_steps + 1
            cpu.step()
            # Prevent infinite looping during branch tests
            assert no_steps < 1000, f'{test_vec.name}: Took too long'

        # Validate final state
        final_state = cpu.get_state()
        assert final_state.pc == test_vec.final.pc, test_vec.name
        assert final_state.sp == test_vec.final.sp, test_vec.name
        assert final_state.a == test_vec.final.a, test_vec.name
        assert final_state.b == test_vec.final.b, test_vec.name
        assert final_state.c == test_vec.final.c, test_vec.name
        assert final_state.d == test_vec.final.d, test_vec.name
        assert final_state.e == test_vec.final.e, test_vec.name
        assert final_state.h == test_vec.final.h, test_vec.name
        assert final_state.l == test_vec.final.l, test_vec.name
        assert final_state.f == test_vec.final.f, test_vec.name
        assert final_state.ime_enabled == test_vec.final.ime, test_vec.name


@pytest.mark.parametrize("op", __get_opcodes())
def test_instr_accuracy(op):
    test_vecs = load_test_vectors(
        f'{BASEURL}/refs/heads/main/v1/{op}.json')
    __evaluate_test_vectors(test_vecs)


@pytest.mark.parametrize("op", __get_opcodes())
def test_cb_instr_accuracy(op):
    test_vecs = load_test_vectors(
        f'{BASEURL}/refs/heads/main/v1/cb%20{op}.json')
    __evaluate_test_vectors(test_vecs)
