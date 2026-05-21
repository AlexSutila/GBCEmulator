#!/usr/bin/env python3
'''
Heads up, this script is heavily vibe coded but the underlying interfaces
were written by hand, so it should be quite robust - dorce
'''

from typing import Any, Iterator

import json
import sys

from irogb_python import (
    Cartridge,
    GameBoyColor,
    load_cart_filesystem,
)


def json_default(obj):
    if isinstance(obj, bytes):
        return obj.hex()
    raise TypeError(f"Type not serializable: {type(obj)}")


def diff_states(lhs: Any, rhs: Any, path: str = "root") -> bool:
    if type(lhs) is not type(rhs):
        print(f"{path}: type mismatch")
        print(f"  lhs: {type(lhs).__name__}")
        print(f"  rhs: {type(rhs).__name__}")
        return False

    if isinstance(lhs, dict):
        lhs_keys = set(lhs.keys())
        rhs_keys = set(rhs.keys())

        only_lhs = lhs_keys - rhs_keys
        only_rhs = rhs_keys - lhs_keys

        if only_lhs:
            for key in sorted(only_lhs):
                print(f"{path}.{key}: only in lhs")
            return False

        if only_rhs:
            for key in sorted(only_rhs):
                print(f"{path}.{key}: only in rhs")
            return False

        for key in sorted(lhs_keys):
            if not diff_states(lhs[key], rhs[key], f"{path}.{key}"):
                return False

        return True

    if isinstance(lhs, list):
        if len(lhs) != len(rhs):
            print(f"{path}: length mismatch")
            print(f"  lhs: {len(lhs)}")
            print(f"  rhs: {len(rhs)}")
            return False

        for i, (a, b) in enumerate(zip(lhs, rhs)):
            if not diff_states(a, b, f"{path}[{i}]"):
                return False

        return True

    if isinstance(lhs, (bytes, bytearray)):
        if lhs != rhs:
            print(f"{path}: bytes differ")

            min_len = min(len(lhs), len(rhs))

            for i in range(min_len):
                if lhs[i] != rhs[i]:
                    print(
                        f"  first differing byte @ {i}: "
                        f"{lhs[i]:02x} != {rhs[i]:02x}"
                    )
                    break

            if len(lhs) != len(rhs):
                print(f"  size mismatch: {len(lhs)} != {len(rhs)}")

            return False

        return True

    if lhs != rhs:
        print(f"{path}: value mismatch")
        print(f"  lhs: {lhs}")
        print(f"  rhs: {rhs}")
        return False

    return True


def make_initial_state(cart: Cartridge) -> bytes:
    '''
    In order to make sure we are comparing apples against apples, we make a
    temporary emulator instance which we use just to get an initial savestate
    which we can use to prime the other two emulator instances.
    '''
    gbc = GameBoyColor(cartridge=cart)  # Will fall out of scope. Totally fine
    return gbc.savestate_serialize()


def cycle_stepped_generator(
    initial_state: bytes,
    cart: Cartridge,
) -> Iterator[tuple[int, dict]]:
    gbc = GameBoyColor(cartridge=cart)
    gbc.savestate_deserialize(initial_state)

    increment = 2  # We use 2 because of how we interlace the fast cycle for double speed
    elapsed_cycles = 0

    while True:
        while not gbc.savestate_ready():
            gbc.step()
            elapsed_cycles += increment

        yield elapsed_cycles, gbc.savestate_as_tree()

        gbc.step()
        elapsed_cycles += increment


def big_step_stepped_generator(
    initial_state: bytes,
    cart: Cartridge,
) -> Iterator[tuple[int, dict]]:
    gbc = GameBoyColor(cartridge=cart)
    gbc.savestate_deserialize(initial_state)

    elapsed_cycles = 0

    while True:
        while not gbc.savestate_ready():
            elapsed_cycles += gbc.big_step()

        yield elapsed_cycles, gbc.savestate_as_tree()
        elapsed_cycles += gbc.big_step()


def main() -> int:
    cart = load_cart_filesystem(sys.argv[1])
    initial_state = make_initial_state(cart)

    big_step_gen = big_step_stepped_generator(initial_state, cart)
    cycle_gen = cycle_stepped_generator(initial_state, cart)

    next(big_step_gen)
    next(cycle_gen)

    for frame_idx, (
        (cycle_elapsed, cycle_state),
        (big_elapsed, big_state),
    ) in enumerate(zip(cycle_gen, big_step_gen), start=1):

        if cycle_elapsed > big_elapsed:
            while cycle_elapsed > big_elapsed:
                big_elapsed, big_state = next(big_step_gen)
                if big_elapsed > cycle_elapsed:
                    print("desync detected (big_elapsed too far ahead)")
                    return 1

        print(
            f"[{frame_idx}] "
            f"cycle={cycle_elapsed} "
            f"big_step={big_elapsed}"
        )

        if cycle_elapsed != big_elapsed:
            print("desync detected")
            return 1

        if not diff_states(cycle_state, big_state):
            print("desync detected")

            with open("cycle_step.json", "w") as f:
                json.dump(cycle_state, f, indent=4, default=json_default)
            with open("big_step.json", "w") as f:
                json.dump(big_state, f, indent=4, default=json_default)

            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
