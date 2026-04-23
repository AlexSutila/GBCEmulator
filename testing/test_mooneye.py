#!/usr/bin/env python3
from irogb_python import (
    eval_mooneye_cpu_state,
    run_mooneye_test,
    make_cart_bytes,
    Cartridge,
)
import requests
import zipfile
import pytest
import io

# See GitHub repository to update
RELEASE = "mts-20240926-1737-443f6e1"
URL = f"https://gekkio.fi/files/mooneye-test-suite/{RELEASE}/{RELEASE}.zip"
PATHS = [
    f"{RELEASE}/acceptance/div_timing.gb",
    f"{RELEASE}/acceptance/call_cc_timing2.gb",
    f"{RELEASE}/acceptance/call_cc_timing.gb",
    f"{RELEASE}/acceptance/call_timing2.gb",
    f"{RELEASE}/acceptance/call_timing.gb",
    f"{RELEASE}/acceptance/div_timing.gb",
    f"{RELEASE}/acceptance/ei_sequence.gb",
    f"{RELEASE}/acceptance/ei_timing.gb",
    f"{RELEASE}/acceptance/halt_ime0_ei.gb",
    f"{RELEASE}/acceptance/halt_ime0_nointr_timing.gb",
    f"{RELEASE}/acceptance/halt_ime1_timing.gb",
    f"{RELEASE}/acceptance/if_ie_registers.gb",
    f"{RELEASE}/acceptance/intr_timing.gb",
    f"{RELEASE}/acceptance/jp_cc_timing.gb",
    f"{RELEASE}/acceptance/jp_timing.gb",
    f"{RELEASE}/acceptance/ld_hl_sp_e_timing.gb",
    f"{RELEASE}/acceptance/add_sp_e_timing.gb",
    f"{RELEASE}/acceptance/pop_timing.gb",
    f"{RELEASE}/acceptance/push_timing.gb",
    f"{RELEASE}/acceptance/rapid_di_ei.gb",
    f"{RELEASE}/acceptance/ret_cc_timing.gb",
    f"{RELEASE}/acceptance/reti_intr_timing.gb",
    f"{RELEASE}/acceptance/reti_timing.gb",
    f"{RELEASE}/acceptance/ret_timing.gb",
    f"{RELEASE}/acceptance/rst_timing.gb",
    f"{RELEASE}/acceptance/instr/daa.gb",
    f"{RELEASE}/acceptance/bits/mem_oam.gb",
    f"{RELEASE}/acceptance/bits/reg_f.gb",
    f"{RELEASE}/acceptance/interrupts/ie_push.gb",
    f"{RELEASE}/acceptance/timer/div_write.gb",
    f"{RELEASE}/acceptance/timer/rapid_toggle.gb",
    f"{RELEASE}/acceptance/timer/tim00_div_trigger.gb",
    f"{RELEASE}/acceptance/timer/tim00.gb",
    f"{RELEASE}/acceptance/timer/tim01_div_trigger.gb",
    f"{RELEASE}/acceptance/timer/tim01.gb",
    f"{RELEASE}/acceptance/timer/tim10_div_trigger.gb",
    f"{RELEASE}/acceptance/timer/tim10.gb",
    f"{RELEASE}/acceptance/timer/tim11_div_trigger.gb",
    f"{RELEASE}/acceptance/timer/tim11.gb",
    f"{RELEASE}/acceptance/timer/tima_reload.gb",
    f"{RELEASE}/acceptance/timer/tima_write_reloading.gb",
    f"{RELEASE}/acceptance/timer/tma_write_reloading.gb",
    f"{RELEASE}/acceptance/ppu/intr_2_0_timing.gb",
    f"{RELEASE}/acceptance/ppu/intr_2_mode0_timing.gb",
    f"{RELEASE}/acceptance/ppu/intr_2_mode0_timing_sprites.gb",
    f"{RELEASE}/acceptance/ppu/intr_2_mode3_timing.gb",
    f"{RELEASE}/acceptance/ppu/intr_2_oam_ok_timing.gb",
    f"{RELEASE}/acceptance/ppu/stat_irq_blocking.gb",
    f"{RELEASE}/acceptance/ppu/stat_lyc_onoff.gb",
    f"{RELEASE}/acceptance/oam_dma/basic.gb",
    f"{RELEASE}/acceptance/oam_dma/reg_read.gb",
    f"{RELEASE}/acceptance/oam_dma_restart.gb",
    f"{RELEASE}/acceptance/oam_dma_start.gb",
    f"{RELEASE}/acceptance/oam_dma_timing.gb",
    f"{RELEASE}/emulator-only/mbc1/bits_bank1.gb",
    f"{RELEASE}/emulator-only/mbc1/bits_bank2.gb",
    f"{RELEASE}/emulator-only/mbc1/bits_mode.gb",
    f"{RELEASE}/emulator-only/mbc1/bits_ramg.gb",
    f"{RELEASE}/emulator-only/mbc1/multicart_rom_8Mb.gb",
    f"{RELEASE}/emulator-only/mbc1/ram_256kb.gb",
    f"{RELEASE}/emulator-only/mbc1/ram_64kb.gb",
    f"{RELEASE}/emulator-only/mbc1/rom_16Mb.gb",
    f"{RELEASE}/emulator-only/mbc1/rom_1Mb.gb",
    f"{RELEASE}/emulator-only/mbc1/rom_2Mb.gb",
    f"{RELEASE}/emulator-only/mbc1/rom_4Mb.gb",
    f"{RELEASE}/emulator-only/mbc1/rom_512kb.gb",
    f"{RELEASE}/emulator-only/mbc1/rom_8Mb.gb",
    f"{RELEASE}/emulator-only/mbc2/bits_ramg.gb",
    f"{RELEASE}/emulator-only/mbc2/bits_romb.gb",
    f"{RELEASE}/emulator-only/mbc2/bits_unused.gb",
    f"{RELEASE}/emulator-only/mbc2/ram.gb",
    f"{RELEASE}/emulator-only/mbc2/rom_1Mb.gb",
    f"{RELEASE}/emulator-only/mbc2/rom_2Mb.gb",
    f"{RELEASE}/emulator-only/mbc2/rom_512kb.gb",
    f"{RELEASE}/emulator-only/mbc5/rom_16Mb.gb",
    f"{RELEASE}/emulator-only/mbc5/rom_1Mb.gb",
    f"{RELEASE}/emulator-only/mbc5/rom_2Mb.gb",
    f"{RELEASE}/emulator-only/mbc5/rom_32Mb.gb",
    f"{RELEASE}/emulator-only/mbc5/rom_4Mb.gb",
    f"{RELEASE}/emulator-only/mbc5/rom_512kb.gb",
    f"{RELEASE}/emulator-only/mbc5/rom_64Mb.gb",
    f"{RELEASE}/emulator-only/mbc5/rom_8Mb.gb",
]


def make_id(path: str) -> str:
    path = path.split("/", 1)[-1]
    path = path.removesuffix(".gb")
    return path.replace("/", "::")


def load_cart_from_url(url: str, *, rom_name: str | None = None) -> Cartridge:
    resp = requests.get(url, timeout=30)
    resp.raise_for_status()

    with zipfile.ZipFile(io.BytesIO(resp.content)) as zf:
        names = [n for n in zf.namelist() if not n.endswith("/")]

        if not names:
            raise RuntimeError("ZIP contains no files")

        if rom_name is None:
            if len(names) != 1:
                raise RuntimeError("ZIP contains multiple files; specify rom_name")
            rom_name = names[0]

        with zf.open(rom_name) as f:
            rom_bytes = f.read()

    return make_cart_bytes(rom_bytes)


@pytest.mark.parametrize("path", PATHS, ids=make_id)
def test_mooneye(path: str):
    cart = load_cart_from_url(URL, rom_name=path)

    # Run until `LD B, B` breakpoint instruction
    state = run_mooneye_test(cart, False)
    assert state is not None, "Took too long"
    eval_mooneye_cpu_state(state)


@pytest.mark.parametrize("path", PATHS, ids=make_id)
def test_mooneye_opt(path: str):
    cart = load_cart_from_url(URL, rom_name=path)

    # Run until `LD B, B` breakpoint instruction
    state = run_mooneye_test(cart, True)
    assert state is not None, "Took too long"
    eval_mooneye_cpu_state(state)
