#!/usr/bin/env python3
from gbc_py import (
    StatModes,
    StatIntFlags,
    STAT,
    LY,
)
import pytest


@pytest.mark.parametrize("flag", [
    StatIntFlags.LYC_EQ_LY,
    StatIntFlags.MODE_0_SEL,
    StatIntFlags.MODE_1_SEL,
    StatIntFlags.MODE_2_SEL,
    StatIntFlags.LYC_SEL,
])
def test_stat_int_flags(flag):
    '''Validate STAT interrupt flags are properly reflected in bitset'''
    stat = STAT()
    assert not stat.int_enabled(flag)
    stat.write(flag)  # Flag is a bitmask, so we can write it as a byte
    assert stat.int_enabled(flag)


@pytest.mark.parametrize("flag", [
    StatModes.MODE_HBLANK,
    StatModes.MODE_VBLANK,
    StatModes.MODE_OAM_SCAN,
    StatModes.MODE_DRAWING,
])
def test_stat_mode_flags(flag):
    '''Validate modes are properly reflected in bitset'''
    stat = STAT()
    stat.mode = flag
    assert (stat.read() & 0x03) == int(flag)


def test_ly_wrap_around():
    '''Validate the range of the LY register before wrapping around'''
    ly = LY()
    ly.reset()
    # Simulate one entire frame, plus one scanline
    for i in range(154):  # Increase until end of frame
        assert i == ly.read()
        ly.inc()
    assert ly.read() == 0  # Should wrap back to zero


def test_ly_read_only():
    '''Validate LY is a read only register'''
    ly = LY()
    ly.reset()
    for _ in range(50):
        ly.inc()
    ly.write(0x00)
    assert ly.read() != 0
