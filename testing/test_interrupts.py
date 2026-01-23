#!/usr/bin/env python3
from gbc_py import (
    InterruptMasterEnable,
    AddressBus,
    GameBoyColor
)


def test_ie_register_unused_bits():
    '''Validate the behavior of the unused bits for the IE register'''
    gbc = GameBoyColor()
    bus = gbc.get_bus()
    bus.init_test_bed()
    assert bus.read_byte(0xFFFF) == 0x00
    bus.write_byte(0xFFFF, 0xFF)
    assert bus.read_byte(0xFFFF) == 0xFF


def test_if_register_unused_bits():
    '''Validate the behavior of the unused bits for the IF register'''
    gbc = GameBoyColor()
    bus = gbc.get_bus()
    bus.init_test_bed()
    bus.write_byte(0xFF0F, 0)  # Write to clear value written by BIOS
    assert bus.read_byte(0xFF0F) == 0xE0
    bus.write_byte(0xFF0F, 0xFF)
    assert bus.read_byte(0xFF0F) == 0xFF


def test_ime_state_boot():
    '''Validate initial state of IME'''
    ime = InterruptMasterEnable()
    for _ in range(100):
        ime.step()
    assert not ime.enabled


def test_ime_enable_ei_timing():
    '''Validate ime enable timing behavior with ei'''
    ime = InterruptMasterEnable()
    assert not ime.enabled

    # Step before instruction execution, emulate EI
    ime.step()
    ime.enable(True)

    # Step before instruction execution, IME is disabled
    ime.step()
    assert not ime.enabled

    # Step before instruction execution, IME is enabled
    ime.step()
    assert ime.enabled


def test_ime_enable_reti_timing():
    '''Validate ime enable timing behavior with ei'''
    ime = InterruptMasterEnable()
    assert not ime.enabled

    # Step before instruction execution, emulate EI
    ime.step()
    ime.enable(False)

    # Step before instruction execution, IME is enabled
    ime.step()
    assert ime.enabled


def test_ime_ei_di_quirk():
    '''EI followed by DI never enables interrupts'''
    ime = InterruptMasterEnable()
    assert not ime.enabled

    # Step before instruction, emulate EI
    ime.step()
    ime.enable(True)

    # Disabled due to one instruction delay
    assert not ime.enabled

    # Step before instruction, emulate DI
    ime.step()
    ime.disable()

    # Interrupts never turn on
    assert not ime.enabled


def test_ime_enable_ei_timing_full():
    '''Test EI instruction timing with actual bytecode'''
    bytecode = [
        0xFB,   # EI  - delay begins
        0x00,   # NOP - delay in progress
        0x00,   # NOP - ime enabled
    ]
    gbc = GameBoyColor()
    gbc.init_test_bed()

    # Component refs
    cpu = gbc.get_cpu()
    bus = gbc.get_bus()

    # Disable boot ROM and write bytecode
    bus.write_byte(0xFF50, 0)
    bus.write_byte(0xFF50, 0)
    entry_point = 0x0100
    for addr, byte in enumerate(bytecode):
        bus.write_byte(entry_point + addr, byte)

    for _ in range(8):
        cpu.step()
    assert not cpu.get_state().ime_enabled
    for _ in range(4):
        cpu.step()
    assert cpu.get_state().ime_enabled


def test_ime_ei_di_quirk_full():
    '''Test EI followed by DI with actual bytecode'''
    bytecode = [
        0xFB,   # EI  - delay begins
        0xF3,   # NOP - delay in progress
        0x00,   # NOP - ime enabled
    ]
    gbc = GameBoyColor()
    gbc.init_test_bed()

    # Component refs
    cpu = gbc.get_cpu()
    bus = gbc.get_bus()

    # Disable boot ROM and write bytecode
    bus.write_byte(0xFF50, 0)
    for addr, byte in enumerate(bytecode):
        bus.write_byte(addr, byte)

    for _ in range(12):
        cpu.step()
    assert not cpu.get_state().ime_enabled
