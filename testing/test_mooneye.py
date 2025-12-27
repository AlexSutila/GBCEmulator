#!/usr/bin/env python3
from gbc_py import (
    poll_mooneye_test,
    load_cart_raw,
    GameBoyColor,
    Cart
)
import requests
import zipfile
import pytest
import io

# See github repository to update
RELEASE = 'mts-20240926-1737-443f6e1'


def __load_cart_from_url(url: str, *, rom_name: str | None = None) -> Cart:
    resp = requests.get(url, timeout=30)
    resp.raise_for_status()

    with zipfile.ZipFile(io.BytesIO(resp.content)) as zf:
        names = [n for n in zf.namelist() if not n.endswith("/")]

        if not names:
            raise RuntimeError("ZIP contains no files")

        if rom_name is None:
            if len(names) != 1:
                raise RuntimeError(
                    "ZIP contains multiple files; specify rom_name"
                )
            rom_name = names[0]

        with zf.open(rom_name) as f:
            rom_bytes = f.read()

    return load_cart_raw(rom_bytes)


@pytest.mark.parametrize(
    "path", [
        # Form the bulk of the test suite and are easily verifiable on hardware
        f'{RELEASE}/acceptance/div_timing.gb',
        f'{RELEASE}/acceptance/call_cc_timing2.gb',
        f'{RELEASE}/acceptance/call_cc_timing.gb',
        f'{RELEASE}/acceptance/call_timing2.gb',
        f'{RELEASE}/acceptance/call_timing.gb',
        f'{RELEASE}/acceptance/div_timing.gb',
        f'{RELEASE}/acceptance/ei_sequence.gb',
        f'{RELEASE}/acceptance/ei_timing.gb',
        f'{RELEASE}/acceptance/halt_ime0_ei.gb',
        f'{RELEASE}/acceptance/halt_ime0_nointr_timing.gb',
        f'{RELEASE}/acceptance/halt_ime1_timing.gb',
        f'{RELEASE}/acceptance/if_ie_registers.gb',
        f'{RELEASE}/acceptance/intr_timing.gb',
        f'{RELEASE}/acceptance/jp_cc_timing.gb',
        f'{RELEASE}/acceptance/jp_timing.gb',
        f'{RELEASE}/acceptance/ld_hl_sp_e_timing.gb',
        f'{RELEASE}/acceptance/oam_dma_restart.gb',
        f'{RELEASE}/acceptance/oam_dma_start.gb',
        f'{RELEASE}/acceptance/oam_dma_timing.gb',
        f'{RELEASE}/acceptance/pop_timing.gb',
        f'{RELEASE}/acceptance/push_timing.gb',
        f'{RELEASE}/acceptance/rapid_di_ei.gb',
        f'{RELEASE}/acceptance/ret_cc_timing.gb',
        f'{RELEASE}/acceptance/reti_intr_timing.gb',
        f'{RELEASE}/acceptance/reti_timing.gb',
        f'{RELEASE}/acceptance/ret_timing.gb',
        f'{RELEASE}/acceptance/rst_timing.gb',
        # Instruction validity
        f'{RELEASE}/acceptance/instr/daa.gb',
        # Unused bits
        f'{RELEASE}/acceptance/bits/mem_oam.gb',
        f'{RELEASE}/acceptance/bits/reg_f.gb',
        # Interrupt behaviors
        f'{RELEASE}/acceptance/interrupts/ie_push.gb',
        # Timer behaviors
        f'{RELEASE}/acceptance/timer/div_write.gb',
        f'{RELEASE}/acceptance/timer/rapid_toggle.gb',
        f'{RELEASE}/acceptance/timer/tim00_div_trigger.gb',
        f'{RELEASE}/acceptance/timer/tim00.gb',
        f'{RELEASE}/acceptance/timer/tim01_div_trigger.gb',
        f'{RELEASE}/acceptance/timer/tim01.gb',
        f'{RELEASE}/acceptance/timer/tim10_div_trigger.gb',
        f'{RELEASE}/acceptance/timer/tim10.gb',
        f'{RELEASE}/acceptance/timer/tim11_div_trigger.gb',
        f'{RELEASE}/acceptance/timer/tim11.gb',
        f'{RELEASE}/acceptance/timer/tima_reload.gb',
        f'{RELEASE}/acceptance/timer/tima_write_reloading.gb',
        f'{RELEASE}/acceptance/timer/tma_write_reloading.gb',
        # Ppu behaviors
        f'{RELEASE}/acceptance/ppu/intr_2_0_timing.gb',
        f'{RELEASE}/acceptance/ppu/intr_2_mode0_timing.gb',
        f'{RELEASE}/acceptance/ppu/intr_2_mode0_timing_sprites.gb',
        f'{RELEASE}/acceptance/ppu/intr_2_mode3_timing.gb',
        f'{RELEASE}/acceptance/ppu/intr_2_oam_ok_timing.gb',
        f'{RELEASE}/acceptance/ppu/stat_irq_blocking.gb',
        f'{RELEASE}/acceptance/ppu/stat_lyc_onoff.gb',
    ]
)
def test_mooneye(path: str):
    url = f'https://gekkio.fi/files/mooneye-test-suite/{RELEASE}/{RELEASE}.zip'
    cart = __load_cart_from_url(url, rom_name=path)

    # Emulator instance, insert cart
    gbc = GameBoyColor()
    gbc.insert_cartridge(cart)
    assert poll_mooneye_test(gbc), 'Took too long'

    # Get CPU state after breakpoint hit
    state = gbc.get_cpu().get_state()
    assert state.b == 3
    assert state.c == 5
    assert state.d == 8
    assert state.e == 13
    assert state.h == 21
    assert state.l == 34
