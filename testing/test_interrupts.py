#!/usr/bin/env python3
from gbc_py import (
    AddressBus,
)


def test_ie_unused_registers():
    '''Validate the behavior of the unused bits for the IE register'''
    bus = AddressBus()
    assert bus.read_byte(0xFFFF) == 0x00
    bus.write_byte(0xFFFF, 0xFF)
    assert bus.read_byte(0xFFFF) == 0xFF


def test_if_unused_registers():
    '''Validate the behavior of the unused bits for the IF register'''
    bus = AddressBus()
    assert bus.read_byte(0xFF0F) == 0xE0
    bus.write_byte(0xFF0F, 0xFF)
    assert bus.read_byte(0xFF0F) == 0xFF
