from __future__ import annotations

from typing import Union
from pathlib import Path

from .gbc_py import SpecialMbc
from . import gbc_py as core


class RomHeader:
    def __init__(self, header: core.RomHeader):
        if not isinstance(header, core.RomHeader):
            raise TypeError("Expected core.RomHeader")
        self._header = header

    @property
    def sgb_flag(self) -> int:
        return self._header.sgb_flag

    @property
    def cgb_flag(self) -> int:
        return self._header.cgb_flag()

    @property
    def title(self) -> str:
        return self._header.title()

    @property
    def manufacturer_code(self) -> str:
        return self._header.manufacturer_code()

    @property
    def cartridge_type_value(self) -> int:
        return self._header.cartridge_type


class Cartridge:
    def __init__(self, cart: core.Cart):
        if not isinstance(cart, core.Cart):
            raise TypeError("Expected core.Cart")
        self._cart = cart

    @property
    def rom(self) -> bytes:
        return self._cart.rom

    @property
    def rom_size(self) -> int:
        return self._cart.rom_size

    @property
    def header(self):
        return RomHeader(self._cart.header)

    @property
    def special_mbc_type(self) -> SpecialMbc:
        return self._cart.special_mbc

    @property
    def _raw(self) -> core.Cart:
        """Internal use only: underlying core.Cart object"""
        return self._cart


def make_cart_bytes(rom_bytes: bytes) -> Cartridge:
    if not isinstance(rom_bytes, (bytes, bytearray, memoryview)):
        raise TypeError("`rom_bytes` must be bytes-like")
    if len(rom_bytes) == 0:
        raise ValueError("`rom_bytes` cannot be empty")
    return Cartridge(core.load_cart_raw(rom_bytes))


def load_cart_filesystem(rom_path: Union[str, Path]) -> Cartridge:
    if not isinstance(rom_path, (str, Path)):
        raise TypeError("`rom_path` must be str or pathlib.Path")
    path = Path(rom_path)

    if not path.exists():
        raise FileNotFoundError(f"Rom not found: {path}")
    if not path.is_file():
        raise ValueError(f"Not a file: {path}")
    return Cartridge(core.load_cart_fs(path))
