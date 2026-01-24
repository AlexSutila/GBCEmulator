#!/usr/bin/env python3
from gbc_py import (
    load_cart_raw,
    GameBoyColor,
    Cart
)
from matplotlib.patches import Rectangle
from typing import Optional, List
import matplotlib.pyplot as plt
import numpy as np
import requests


def __load_cart_from_url(url: str) -> Cart:
    resp = requests.get(url, timeout=30)
    resp.raise_for_status()
    return load_cart_raw(resp.content)


def __run_and_get_frame(url: str) -> List[int]:
    cart = __load_cart_from_url(url)
    gbc = GameBoyColor()
    gbc.insert_cartridge(cart)

    # Runs for roughly one minute
    gbc.step_cycles(70224 * 60 * 60)
    return gbc.get_frame()


def __render_from_url(url):
    frame = __run_and_get_frame(url)
    frame = np.asarray(frame, dtype=np.uint32)

    pixels = frame.view(np.uint8).reshape((144, 160, 4))
    img = pixels[..., [2, 1, 0]]  # ARGB8888 → RGB
    return img


def __run_test_set(
    urls: List[str],
    out_path: str,
    titles: Optional[List[str]] = None,
):
    images = [__render_from_url(url) for url in urls]
    n = len(images)

    if titles is not None and len(titles) != n:
        raise ValueError("titles must be the same length as urls")

    fig, axes = plt.subplots(1, n, figsize=(4 * n, 4))
    if n == 1:
        axes = [axes]

    for i, (ax, img) in enumerate(zip(axes, images)):
        ax.imshow(img)
        ax.set_xticks([])
        ax.set_yticks([])

        h, w, _ = img.shape
        rect = Rectangle(
            (0, 0),
            w,
            h,
            linewidth=1.5,
            edgecolor="black",
            facecolor="none",
        )
        ax.add_patch(rect)

        if titles is not None:
            ax.text(
                0.5,
                -0.08,
                titles[i],
                transform=ax.transAxes,
                ha="center",
                va="top",
                rotation=-10,
                fontsize=10,
            )

    plt.tight_layout()
    plt.savefig(out_path, bbox_inches="tight", dpi=150)


def run_acid_test_suite():
    __run_test_set(
        urls=[
            'https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb',
            'https://github.com/mattcurrie/cgb-acid2/releases/download/v1.1/cgb-acid2.gbc'
        ],
        titles=[
            'dmg-acid',
            'cgb-acid'
        ],
        out_path='assets/acid_tests.png'
    )


def run_blargg_cpu_tests():
    __run_test_set(
        urls=[
            'https://github.com/retrio/gb-test-roms/raw/refs/heads/master/cpu_instrs/cpu_instrs.gb',
            'https://github.com/retrio/gb-test-roms/raw/refs/heads/master/mem_timing/mem_timing.gb',
            'https://github.com/retrio/gb-test-roms/raw/refs/heads/master/mem_timing-2/mem_timing.gb',
            'https://github.com/retrio/gb-test-roms/raw/refs/heads/master/instr_timing/instr_timing.gb',
            'https://github.com/retrio/gb-test-roms/raw/refs/heads/master/interrupt_time/interrupt_time.gb',
        ],
        titles=[
            'cpu_instrs',
            'mem_timing',
            'mem_timing2',
            'instr_timing',
            'interrupt_time'
        ],
        out_path='assets/blargg_cpu_mem.png'
    )


if __name__ == '__main__':
    run_acid_test_suite()
    run_blargg_cpu_tests()
