#!/usr/bin/env python3
from irogb_python import (
    load_cart_filesystem,
    SpecialMbc,
    Cartridge,
)
import pytest


def _get_cart_type_str(cart: Cartridge) -> str:
    '''
    We aren't too critical of detail here, the main motivation behind these
    tests is to ensure our heuristics do not break compatability with any
    mainstream roms.
    '''
    if cart.special_mbc_type != SpecialMbc.NotSpecial:
        match cart.special_mbc_type:
            case SpecialMbc.MMM01:
                return "MMM01"
            case SpecialMbc.MBC1M:
                return "MBC1"
            case SpecialMbc.MBC30:
                return "MBC30"

    # We **really** do not want to break these with bad heuristics
    match cart.header.cartridge_type_value:
        case 0x00 | 0x08 | 0x09:
            return ""
        case 0x01 | 0x02 | 0x03:
            return "MBC1"
        case 0x05 | 0x06:
            return "MBC2"
        case 0x0B | 0x0C | 0x0D:
            return "MMM01"
        case 0x0F | 0x10 | 0x11 | 0x12 | 0x13:
            return "MBC3"
        case 0x19 | 0x1A | 0x1B | 0x1C | 0x1D | 0x1E:
            return "MBC5"
        case 0x22:
            return "MBC7"
        case 0xFD:
            return "TAMA5"
        case 0xFE:
            return "HuC-3"
        case 0xFF:
            return "HuC-1"
        case _:
            return "???"


@pytest.mark.heuristic
def test_rom(case):
    assert case.path.exists()

    cart = load_cart_filesystem(rom_path=case.path)
    val = _get_cart_type_str(cart)
    assert val in case.u2_kind or val in case.u1_kind, f'{
        case.path} - got {val}'
