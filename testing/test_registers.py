#!/usr/bin/env python3
from gbc_py import (
    CpuFlagsRegister,
    CpuRegister,
    StatusFlagMask,
)
import pytest


def test_cpureg_init():
    '''CpuRegister: Check to ensure initial state is zero'''
    r = CpuRegister()
    assert r.full == 0
    assert r.lo == 0
    assert r.hi == 0


def test_cpureg_8_bit_write_16_bit_read():
    '''CpuRegister: Validate functionality of writing lo and hi'''
    r = CpuRegister()
    r.lo, r.hi = 0x34, 0x12
    assert r.full == 0x1234


def test_cpureg_16_bit_write_8_bit_read():
    '''CpuRegister: Validate functionality of writing full'''
    r = CpuRegister()
    r.full = 0x1234
    assert r.lo == 0x34
    assert r.hi == 0x12


def test_cpuflags_init():
    '''CpuFlagsRegister: Check to ensure initial state is zero'''
    r = CpuFlagsRegister()
    assert r.full == 0
    assert r.lo == 0
    assert r.hi == 0

    # Check each individual flag as well as raw data
    assert not r.get_flag(StatusFlagMask.Z)
    assert not r.get_flag(StatusFlagMask.N)
    assert not r.get_flag(StatusFlagMask.H)
    assert not r.get_flag(StatusFlagMask.C)


@pytest.mark.parametrize(
    "flag_mask", [
        StatusFlagMask.Z,
        StatusFlagMask.N,
        StatusFlagMask.H,
        StatusFlagMask.C,
    ],
)
def test_cpuflags_set_flag_and_get(flag_mask):
    '''CpuFlagsRegister: Set flag, ensure it is reflected getter'''
    r = CpuFlagsRegister()
    assert not r.get_flag(flag_mask)

    # Should only be true after setting flag
    r.set_flag(flag_mask)
    assert r.get_flag(flag_mask)


@pytest.mark.parametrize(
    "flag_mask", [
        StatusFlagMask.Z,
        StatusFlagMask.N,
        StatusFlagMask.H,
        StatusFlagMask.C,
    ],
)
def test_cpuflags_set_flag_and_read(flag_mask):
    '''CpuFlagsRegister: Set flag, ensure it is reflected in raw state'''
    r = CpuFlagsRegister()
    assert not r.get_flag(flag_mask)
    assert r.full == 0
    assert r.lo == 0
    assert r.hi == 0
    r.set_flag(flag_mask)

    # Should reflect in raw state after setting
    assert r.full == int(flag_mask)
    assert r.lo == int(flag_mask)

    # High is accumulator, not impacted by flags
    assert r.hi == 0


def test_cpuflags_unused_bits():
    '''CpuRegisterFlags: Set flags, ensure unused bits always read zero'''
    r = CpuFlagsRegister()
    r.full = 0xFF
    assert r.full == 0xF0
