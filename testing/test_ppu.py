#!/usr/bin/env python3
from gbc_py import (
    AddressBus,
    PixelProcessor
)


def __get_ppu_mode(bus: AddressBus) -> int:
    return bus.read_byte(0xFF41) & 0x3


def test_ppu_mode_timing():
    '''Basic validation of PPU mode timings'''
    bus = AddressBus()
    bus.init_test_bed()

    # Obtain ppu reference and enable
    ppu = PixelProcessor(bus)
    bus.write_byte(0xFF40, 0x80)

    for frame in range(10):
        for ly in range(144):
            for dot in range(80):
                assert __get_ppu_mode(bus) == 2, f'dot: {dot}, ly: {ly}'
                ppu.step()
            for dot in range(289):
                # TODO: Need a better way to test this
                # assert __get_ppu_mode(bus) == 3, f'dot: {dot}, ly: {ly}'
                ppu.step()
            for dot in range(87):
                assert __get_ppu_mode(bus) == 0, f'dot: {dot}, ly: {ly}'
                ppu.step()
        for ly in range(10):
            for dot in range(456):
                assert __get_ppu_mode(bus) == 1, f'dot: {dot}, ly: {ly}'
                ppu.step()
        assert __get_ppu_mode(bus) == 2, 'Second frame'
