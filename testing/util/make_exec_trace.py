#!/usr/bin/env python3
from gbc_py import (
    poll_mooneye_test,
    load_cart_fs,
    GameBoyColor,
    BreakReason,
)
import sys


def break_cb():
    global gbc  # TODO: We should take GBC in as an arg someday
    state = gbc.get_cpu().get_state()
    disasm = gbc.get_cpu().disasm()

    print(f"PC:{state.pc:04X} SP:{state.sp:04X} : "
          f"A:{state.a:02X} B:{state.b:02X} C:{state.c:02X} "
          f"D:{state.d:02X} E:{state.e:02X} H:{state.h:02X} L:{state.l:02X} "
          f"F:{state.f:02X} IME:{int(state.ime_enabled)} : {disasm}")
    return BreakReason.BRK_STEP_INSTRUCTION


if __name__ == "__main__":
    gbc = GameBoyColor(break_cb)

    # Load ROM (mooneye, but not hard to change)
    cart = load_cart_fs(sys.argv[1])
    gbc.insert_cartridge(cart)

    # Break on entry point
    gbc.breakpoint_add(0x100, BreakReason.BRK_ADDRESS_EXECUTED)
    poll_mooneye_test(gbc)
