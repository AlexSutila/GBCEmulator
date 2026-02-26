#!/usr/bin/env python3
from irogb_python import (
    load_cart_filesystem,
    GameBoyColor,
)
from matplotlib.patches import Rectangle
import matplotlib.pyplot as plt
from typing import List
import os


def run_and_get_frame(path: str, seconds: int) -> List[int]:
    cart = load_cart_filesystem(path)
    gbc = GameBoyColor(cartridge=cart)

    for _ in range(seconds * 60):
        gbc.step_frame()
    return gbc.frame.as_numpy()


def __run_test_set(
    paths: List[str],
    seconds: List[int],
    out_path: str,
):
    images = [
        run_and_get_frame(path, second)
        for path, second in zip(paths, seconds)
    ]
    n = len(images)

    rows, cols = 2, 4
    fig, axes = plt.subplots(rows, cols, figsize=(4 * cols, 4 * rows))
    axes = axes.ravel()

    for ax, img in zip(axes, images):
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

    # Hide unused subplots if fewer than 8 images
    for ax in axes[n:]:
        ax.axis("off")

    plt.tight_layout()
    plt.savefig(out_path, bbox_inches="tight", dpi=150)


def make_dmg_demo():
    __run_test_set(
        paths=[
            f'{os.getenv('ROMS')}/kirby.gb',
            f'{os.getenv('ROMS')}/duck_tales.gb',
            f'{os.getenv('ROMS')}/smb2.gb',
            f'{os.getenv('ROMS')}/smb.gb',
            f'{os.getenv('ROMS')}/zelda.gb',
            f'{os.getenv('ROMS')}/tetris.gb',
            f'{os.getenv('ROMS')}/pk_red.gb',
            f'{os.getenv('ROMS')}/castlevania.gb',
        ],
        seconds=[8, 5, 5, 7, 65, 20, 30, 15],
        out_path='assets/dmg_demo.png'
    )


def make_cgb_demo():
    __run_test_set(
        paths=[
            f'{os.getenv('ROMS')}/pk_crystal.gbc',
            f'{os.getenv('ROMS')}/pk_silver.gbc',
            f'{os.getenv('ROMS')}/pk_yellow.gbc',
            f'{os.getenv('ROMS')}/tetris.gbc',
            f'{os.getenv('ROMS')}/zelda.gbc',
            f'{os.getenv('ROMS')}/shantae.gbc',
            f'{os.getenv('ROMS')}/wario3.gbc',
            f'{os.getenv('ROMS')}/smb.gbc',
        ],
        seconds=[66, 66, 38, 20, 65, 25, 45, 15],
        out_path='assets/cgb_demo.png'
    )


if __name__ == '__main__':
    make_dmg_demo()
    make_cgb_demo()
