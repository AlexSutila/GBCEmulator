#!/usr/bin/env python3
from irogb_python import (
    make_cart_bytes,
    GameBoyColor,
    BreakReason,
)
import requests

URL = "https://github.com/EricKirschenmann/MBC3-Tester-gb/releases/download/v1.0/mbctest.gb"


def test_mbc3():
    """
    Checks to ensure all values from all banks using MBC3 work properly, this
    is tricky for some emulators because niche variants of MBC3 exist and it
    requires a bit of extra work to detect them.
    """
    resp = requests.get(URL, timeout=30)
    resp.raise_for_status()

    cart, expects = make_cart_bytes(resp.content), resp.content[0x4000::0x4000]
    gbc, reads = None, []

    def breakpoint_cb():
        byte_read = gbc.read_byte(0x4000)
        reads.append(byte_read)
        return BreakReason.BRK_CONTINUE

    gbc = GameBoyColor(cartridge=cart, dbg_callback=breakpoint_cb)
    gbc.debugger.breakpoint_add(0x4000, BreakReason.BRK_ADDRESS_READ)
    for _ in range(60):
        gbc.step_frame(big_step=True)

    for read, expect in zip(reads, expects):
        assert read == expect
