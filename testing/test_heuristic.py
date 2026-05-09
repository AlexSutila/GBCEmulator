#!/usr/bin/env python3
from irogb_python import (
    load_cart_filesystem,
    SpecialMbc,
    Cartridge,
)
from pathlib import Path
import pytest


MANI_MULTICART_TITLES = [
    "Mani 4 in 1 - Bubble Bobble + Elevator Action + Chase H.Q. + Sagaia (China) (En).gb",
    "Mani 4 in 1 - Bubble Bobble + Elevator Action + Chase H.Q. + Sagaia (China) (En).gb",
    "Mani 4 in 1 - Genki Bakuhatsu Gambaruger + Zettai Muteki Raijin-Oh + Zoids Densetsu + Miracle Adventure of Esparks - Ushinawareta Seiseki Perivron (China) (Ja).gb",
    "Mani 4 in 1 - R-Type II + Saigo no Nindou + Ganso!! Yancha Maru + Shisenshou - Match-Mania (China) (Ja).gb",
    "Mani 4 in 1 - Takahashi Meijin no Bouken-jima II + GB Genjin + Bomber Boy + Milon no Meikyuu Kumikyoku (China) (Ja).gb",
    "Mani 4 in 1 - Tetris + Alleyway + Yakuman + Tennis (China) (Ja).gb",
]

SACHEN_MMC2_TITLES = [
    "31-in-1 Mighty Mix (Australia) (31B-001, Sachen) (Unl).gbc",
    "31 in 1 (Taiwan) (31B-001, Sachen) (Unl).gbc",
    "4 in 1 + 8 in 1 (World) (4B-001, 4B-009, 8B-001, Sachen) (Unl).gbc",
    "4 in 1 + 8 in 1 (World) (4B-002, 4B-004, 8B-002, Sachen) (Unl).gbc",
    "4 in 1 + 8 in 1 (World) (4B-005, 4B-006, 8B-003, Sachen) (Unl).gbc",
    "4 in 1 + 8 in 1 (World) (4B-007, 4B-008, 8B-004, Sachen) (Unl).gbc",
    "Beast Fighter (Taiwan) (En) (Sachen) (Unl).gbc",
    "Jurassic Boy 2 (Taiwan) (En) (Rev 1) (Sachen) (Unl).gbc",
    "Jurassic Boy 2 + Thunder Blast Man (Taiwan) (En) (1B-002, 1B-003, Sachen) (Unl).gbc",
    "Rocman X Gold + 4 in 1 (Taiwan) (1B-002, 4B-003, Sachen) (Unl).gbc",
    "Street Hero (Taiwan) (En) (1B-004, EB-004, Sachen) (Unl).gbc",
    "Super 16 in 1 (Taiwan) (En) (Sachen) (Unl).gbc",
    "Super 6 in 1 (Taiwan) (En,Zh) (6B-001, Sachen) (Unl).gbc",
    "Thunder Blast Man (Europe) (Sachen) (Unl).gbc",
    "4 in 1 (Europe) (4B-001, Sachen-Commin) (Unl).gb",
    "4 in 1 (Europe) (4B-002, Sachen) (Unl).gb",
    "4 in 1 (Europe) (4B-004, Sachen-Commin) (Unl).gb",
    "4 in 1 (Europe) (4B-005, Sachen-Commin) (Unl).gb",
    "4 in 1 (Europe) (4B-006, Sachen) (Unl).gb",
    "4 in 1 (Europe) (4B-007, Sachen) (Unl).gb",
    "4 in 1 (Europe) (4B-008, Sachen) (Unl).gb",
    "4 in 1 (Europe) (4B-009, Sachen) (Unl).gb",
    "4 in 1 (Taiwan) (En,Zh) (4B-003, Sachen-Commin) (Unl).gb",
]

WISDOM_TREE_TITLES = [
    "King James Bible (USA) (Unl).gb",
    "NIV Bible & the 20 Lost Levels of Joshua (USA) (Unl).gb",
    "Spiritual Warfare (USA) (Unl).gb",
]


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
            case SpecialMbc.Sachen:
                return "Sachen"
            case SpecialMbc.WisdomTree:
                return "WisdomTree"

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
def test_heuristic_mainstream(case):
    assert case.path.exists()

    cart = load_cart_filesystem(rom_path=case.path)
    val = _get_cart_type_str(cart)
    assert val in case.u2_kind or val in case.u1_kind, f'{case.path} - got {val}'


def _test_manual(rom_path: Path, expected: str):
    cart = load_cart_filesystem(rom_path=rom_path)
    val = _get_cart_type_str(cart)
    assert val == expected, f'{rom_path} got {val}'


@pytest.mark.heuristic
@pytest.mark.parametrize("rom_name", MANI_MULTICART_TITLES)
def test_manual_mmm01(rom_name, rom_paths):
    rom_path = next((r for r in rom_paths if r.name == rom_name), None)
    assert rom_path is not None and rom_path.exists(), f'{rom_name} not found'
    _test_manual(rom_path, "MMM01")


@pytest.mark.heuristic
@pytest.mark.parametrize("rom_name", SACHEN_MMC2_TITLES)
def test_manual_sachen(rom_name, rom_paths):
    rom_path = next((r for r in rom_paths if r.name == rom_name), None)
    assert rom_path is not None and rom_path.exists(), f'{rom_name} not found'
    _test_manual(rom_path, "Sachen")


@pytest.mark.heuristic
@pytest.mark.parametrize("rom_name", WISDOM_TREE_TITLES)
def test_manual_wisdom_tree(rom_name, rom_paths):
    rom_path = next((r for r in rom_paths if r.name == rom_name), None)
    assert rom_path is not None and rom_path.exists(), f'{rom_name} not found'
    _test_manual(rom_path, "WisdomTree")
