#!/usr/bin/env python3
from gbc_py import (
    AddressBus,
    get_boot_rom,
)
import hashlib


def test_boot_rom_bus_dump():
    '''Validate bus dump of boot ROM against ground truth'''
    bus = AddressBus()
    rom = get_boot_rom()

    # Generate a bus dump of what should be the boot ROM
    bus_content = [bus.read_byte(addr) for addr in range(len(rom))]
    bus_md5 = hashlib.md5(bytes(bus_content)).hexdigest()

    # Will verify using md5sum hashes of the raw binary and a bus dump
    true_md5 = hashlib.md5(rom).hexdigest()
    assert bus_md5 == true_md5


def test_boot_rom_banking():
    '''Validate the writing the control register to bank out of boot ROM'''
    bus = AddressBus()

    # Take the first byte of the ROM, and validate content
    first_byte = get_boot_rom()[0]
    assert bus.read_byte(0x0) == first_byte

    # Write the control register, validate value change
    bus.write_byte(0xFF50, 0x0)
    assert bus.read_byte(0x0) != first_byte
