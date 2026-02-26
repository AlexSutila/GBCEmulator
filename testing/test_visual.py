#!/usr/bin/env python3
from irogb_python import (
    make_cart_bytes,
    GameBoyColor,
    Cartridge,
)
from matplotlib.patches import Rectangle
from typing import Optional, List
import matplotlib.pyplot as plt
import numpy as np
import requests
import hashlib
import pytest

# ============================================================
# Test Data
# ============================================================

ACID_CASES = [
    (
        "dmg-acid",
        "https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb",
        "7627d38eef6910e472e6901687a20075",
    ),
    (
        "cgb-acid",
        "https://github.com/mattcurrie/cgb-acid2/releases/download/v1.1/cgb-acid2.gbc",
        "1655ee04a3fa1a81b0aa23243782072b",
    ),
]


BLARGG_CASES = [
    (
        "cpu_instrs",
        "https://github.com/retrio/gb-test-roms/raw/refs/heads/master/cpu_instrs/cpu_instrs.gb",
        "8312df229968ab0cb53a7ba673e30168",
    ),
    (
        "mem_timing",
        "https://github.com/retrio/gb-test-roms/raw/refs/heads/master/mem_timing/mem_timing.gb",
        "554fb47953352ccef7ee889e275662f2",
    ),
    (
        "mem_timing2",
        "https://github.com/retrio/gb-test-roms/raw/refs/heads/master/mem_timing-2/mem_timing.gb",
        "b97d36f4779d3dfb0a374d6e566e115c",
    ),
    (
        "cgb_sound",
        "https://github.com/retrio/gb-test-roms/raw/refs/heads/master/cgb_sound/cgb_sound.gb",
        "4d472d573485a985c6afaa675222c596",
    ),
    (
        "instr_timing",
        "https://github.com/retrio/gb-test-roms/raw/refs/heads/master/instr_timing/instr_timing.gb",
        "9c025942d2be5c03dabd275dfc2b505e",
    ),
    (
        "interrupt_time",
        "https://github.com/retrio/gb-test-roms/raw/refs/heads/master/interrupt_time/interrupt_time.gb",
        "2573fa82ee7de56a77699a543bba0ab0",
    ),
    (
        "halt_bug",
        "https://github.com/retrio/gb-test-roms/raw/refs/heads/master/halt_bug.gb",
        "c2eb31079786aacee3fee865d88b4b8e",
    ),
    # TODO: We fail this test, hence no valid md5sum
    (
        "oam_bug",
        "https://github.com/retrio/gb-test-roms/raw/refs/heads/master/oam_bug/oam_bug.gb",
        "00000000000000000000000000000000",
    ),
]


def load_cart_from_url(url: str) -> Cartridge:
    resp = requests.get(url, timeout=30)
    resp.raise_for_status()
    return make_cart_bytes(resp.content)


def run_and_get_frame(url: str) -> List[int]:
    cart = load_cart_from_url(url)
    gbc = GameBoyColor(cart)

    # Runs for roughly one minute
    for _ in range(60 * 60):
        gbc.step_frame()
    return gbc.frame.as_numpy()


def to_digest(img: np.ndarray) -> str:
    img = np.ascontiguousarray(img, dtype=np.uint32)
    return hashlib.md5(img.tobytes()).hexdigest()


def run_test_set_rendered(
    urls: List[str],
    out_path: str,
    rows: int,
    cols: int,
    titles: Optional[List[str]] = None,
):
    images = [run_and_get_frame(url) for url in urls]
    n = len(images)

    if titles is not None and len(titles) != n:
        raise ValueError("titles must be the same length as urls")

    fig, axes = plt.subplots(rows, cols, figsize=(4 * cols, 4 * rows))
    axes = axes.ravel()

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
                -0.03,
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
    run_test_set_rendered(
        urls=[i[1] for i in ACID_CASES],
        titles=[i[0] for i in ACID_CASES],
        rows=1,
        cols=2,
        out_path='assets/acid_tests.png'
    )


def run_blargg_cpu_tests():
    run_test_set_rendered(
        urls=[i[1] for i in BLARGG_CASES],
        titles=[i[0] for i in BLARGG_CASES],
        rows=2,
        cols=4,
        out_path='assets/blargg_cpu_mem.png'
    )


@pytest.mark.parametrize("title,url,expected_md5", ACID_CASES)
def test_acid_suite(title: str, url: str, expected_md5: str):
    img = run_and_get_frame(url)
    digest = to_digest(img)
    assert digest == expected_md5, f"{title} failed (got {digest})"


@pytest.mark.parametrize("title,url,expected_md5", BLARGG_CASES)
def test_blargg_suite(title: str, url: str, expected_md5: str):
    img = run_and_get_frame(url)
    digest = to_digest(img)
    assert digest == expected_md5, f"{title} failed (got {digest})"


if __name__ == '__main__':
    run_acid_test_suite()
    run_blargg_cpu_tests()
