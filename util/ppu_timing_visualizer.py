#!/usr/bin/env python3
from gbc_py import (
    GameBoyColor
)
import pandas as pd

scanline_length, nr_scanlines = 456, 154
gbc = GameBoyColor()
gbc.init_test_bed()

gbc.get_bus().write_byte(0xFF50, 0x00)  # Disable boot rom
gbc.get_bus().write_byte(0xFF40, 0x80)  # Enable PPU


def run_scanline():
    bus = gbc.get_bus()
    stat_mode_list = []
    ly_list = []
    clocks_list = []

    # We only want to see clocks in multiples of four
    for dot in range(0, scanline_length, 4):
        stat_mode_list.append(bus.read_byte(0xFF41) & 0x3)
        ly_list.append(bus.read_byte(0xFF44))
        clocks_list.append(dot)
        for _ in range(4):
            gbc.step()

    df = pd.DataFrame(
        {
            'LY Register': ly_list,
            'STAT Mode': stat_mode_list,
        },
        index=pd.Index(clocks_list, name='Clocks')
    )
    mask = (
        ((df.index >= 0) & (df.index <= 4)) |
        ((df.index >= 72) & (df.index <= 84)) |
        (df.index >= 444)
    )

    filtered = df.loc[mask]
    return filtered.T


if __name__ == '__main__':
    show = [0, 1, 143, 144, 152, 153]
    for scanline in range(nr_scanlines):
        # We prefer to see the timing one frame in
        run_scanline()
    for scanline in range(nr_scanlines):
        df = run_scanline()
        if scanline in show:
            print(f'Line {scanline}:\n{df}\n')
