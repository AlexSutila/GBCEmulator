#!/usr/bin/env python3
from gbc_py import (
    load_cart_fs,
    GameBoyColor,
)
from matplotlib.patches import Rectangle
import matplotlib.pyplot as plt
from typing import List
import numpy as np


def __run_and_get_frame(path: str, seconds: int) -> List[int]:
    cart = load_cart_fs(path)
    gbc = GameBoyColor()
    gbc.insert_cartridge(cart)

    gbc.step_cycles(70224 * 60 * seconds)
    return gbc.get_frame()


def __render_from_path(path, seconds):
    frame = __run_and_get_frame(path, seconds)
    frame = np.asarray(frame, dtype=np.uint32)

    pixels = frame.view(np.uint8).reshape((144, 160, 4))
    img = pixels[..., [2, 1, 0]]  # ARGB8888 → RGB
    return img


def __run_test_set(
    paths: List[str],
    seconds: List[int],
    out_path: str,
):
    images = [__render_from_path(path, second)
              for path, second in zip(paths, seconds)]
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
            '/home/dorce/Documents/git/GBCEmulator/roms/kirby.gb',
            '/home/dorce/Documents/git/GBCEmulator/roms/duck_tales.gb',
            '/home/dorce/Documents/git/GBCEmulator/roms/smb2.gb',
            '/home/dorce/Documents/git/GBCEmulator/roms/smb.gb',
            '/home/dorce/Documents/git/GBCEmulator/roms/zelda.gb',
            '/home/dorce/Documents/git/GBCEmulator/roms/pk_blue.gb',
            '/home/dorce/Documents/git/GBCEmulator/roms/pk_red.gb',
            '/home/dorce/Documents/git/GBCEmulator/roms/castlevania.gb',
        ],
        seconds=[8, 5, 5, 7, 65, 30, 30, 15],
        out_path='assets/dmg_demo.png'
    )


def make_cgb_demo():
    __run_test_set(
        paths=[
            '/home/dorce/Documents/git/GBCEmulator/roms/pk_crystal.gbc',
            '/home/dorce/Documents/git/GBCEmulator/roms/pk_silver.gbc',
            '/home/dorce/Documents/git/GBCEmulator/roms/pk_yellow.gbc',
            '/home/dorce/Documents/git/GBCEmulator/roms/tetris.gbc',
            '/home/dorce/Documents/git/GBCEmulator/roms/zelda.gbc',
            '/home/dorce/Documents/git/GBCEmulator/roms/zelda_oracle_of_seasons.gbc',
            '/home/dorce/Documents/git/GBCEmulator/roms/zelda_oracle_of_ages.gbc',
            '/home/dorce/Documents/git/GBCEmulator/roms/smb.gbc',
        ],
        seconds=[66, 66, 40, 20, 65, 100, 100, 15],
        out_path='assets/cgb_demo.png'
    )


if __name__ == '__main__':
    make_dmg_demo()
    make_cgb_demo()
