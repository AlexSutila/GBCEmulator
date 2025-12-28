#!/usr/bin/env python3
from gbc_py import (
    AddressBus,
    GameBoyColor,
    TimerUnit,
)


def read_div(bus: AddressBus) -> int:
    return bus.read_byte(0xFF04)


def read_tima(bus: AddressBus) -> int:
    return bus.read_byte(0xFF05)


def write_tima(bus: AddressBus, v: int):
    bus.write_byte(0xFF05, v & 0xFF)


def write_tma(bus: AddressBus, v: int):
    bus.write_byte(0xFF06, v & 0xFF)


def write_tac(bus: AddressBus, v: int):
    bus.write_byte(0xFF07, v & 0x07)


def test_div_increments_every_256_cycles():
    ''' Test DIV increment frequency '''
    gbc = GameBoyColor()
    bus = gbc.get_bus()
    bus.init_test_bed()

    timer = gbc.get_timer()
    write_tac(bus, 0x04)

    start = read_div(bus)
    for i in range(255):
        timer.step()
        assert read_div(bus) == start, f"DIV changed early at cycle {i}"

    timer.step()
    assert read_div(bus) == ((start + 1) & 0xFF)


def test_tima_does_not_tick_when_disabled():
    ''' Ensure the timer does not increment when the timer is off '''
    gbc = GameBoyColor()
    bus = gbc.get_bus()
    bus.init_test_bed()
    timer = gbc.get_timer()

    write_tima(bus, 0)
    write_tac(bus, 0x00)

    for _ in range(5000):
        timer.step()
    assert read_tima(bus) == 0


def test_tima_ticks_every_16_cycles():
    ''' Validate 262144 Hz frequency '''
    gbc = GameBoyColor()
    bus = gbc.get_bus()
    bus.init_test_bed()
    timer = gbc.get_timer()

    write_tima(bus, 0)
    write_tac(bus, 0b101)

    for i in range(15):
        timer.step()
        assert read_tima(bus) == 0, f"Early tick at {i}"

    timer.step()
    assert read_tima(bus) == 1

    for _ in range(16):
        timer.step()

    assert read_tima(bus) == 2


def test_tima_overflow_and_reload():
    ''' Tests behavior of timer overflow  '''
    gbc = GameBoyColor()
    bus = gbc.get_bus()
    bus.init_test_bed()
    timer = gbc.get_timer()

    write_tma(bus, 0xAB)
    write_tima(bus, 0xFF)
    write_tac(bus, 0b101)  # fast mode

    # 16 cycles → overflow triggered
    for _ in range(16):
        timer.step()

    # Hardware delay before reload
    assert read_tima(bus) == 0x00

    # Reload happens a few cycles later
    for _ in range(4):
        timer.step()

    assert read_tima(bus) == 0xAB
