#!/usr/bin/env python3
from gbc_py import (
    GameBoyColor
)
from matplotlib.colors import ListedColormap
from matplotlib.patches import Patch
import matplotlib.pyplot as plt
import numpy as np

scanline_length, nr_scanlines = 456, 154

# 2D indexed framebuffer: [scanline][dot]
fb = np.zeros((nr_scanlines, scanline_length), dtype=np.uint8)

gbc = GameBoyColor()
gbc.init_test_bed()

bus = gbc.get_bus()
bus.write_byte(0xFF50, 0x00)  # Disable boot ROM
bus.write_byte(0xFF40, 0x80)  # Enable PPU


def run_scanline(cur_scanline):
    for dot in range(scanline_length):
        # STAT mode is 0–3
        fb[cur_scanline, dot] = bus.read_byte(0xFF41) & 0x3
        gbc.step()


if __name__ == "__main__":
    cmap = ListedColormap([
        "green",  # Mode 0: HBlank
        "cyan",  # Mode 1: VBlank
        "yellow",  # Mode 2: OAM
        "orange",  # Mode 3: Transfer
    ])
    for scanline in range(nr_scanlines):
        run_scanline(scanline)

    fig = plt.figure(
        figsize=(scanline_length / 100, nr_scanlines / 100),
        dpi=1000
    )
    ax_img = fig.add_axes([0.0, 0.05, 1.0, 0.95])
    ax_img.imshow(
        fb,
        cmap=cmap,
        interpolation="nearest",
        vmin=0,
        vmax=3,
        aspect="auto",
    )
    ax_img.axis("off")

    ax_leg = fig.add_axes([0.0, 0.0, 1.0, 0.05])
    ax_leg.axis("off")

    legend_elements = [
        Patch(facecolor="green", label="Mode 0 – HBlank"),
        Patch(facecolor="cyan", label="Mode 1 – VBlank"),
        Patch(facecolor="yellow", label="Mode 2 – OAM"),
        Patch(facecolor="orange", label="Mode 3 – Transfer"),
    ]
    ax_leg.legend(handles=legend_elements, loc="best",
                  ncol=1, frameon=False, fontsize=10,)

    # Show VBLANK start timing
    ax_img.axhline(y=144 - 0.5, color="black", linewidth=1, alpha=0.8)

    # Show Render start timing
    ax_img.axvline(x=80 - 0.5, color="black", linewidth=1, alpha=0.8)

    # Show Render end timing window
    ax_img.axvline(x=80 + 174 - 0.5, color="blue", linewidth=1, alpha=0.8)
    ax_img.axvline(x=80 + 289 - 0.5, color="blue", linewidth=1, alpha=0.8)

    plt.savefig("demo.png", bbox_inches="tight", pad_inches=0)
    plt.close(fig)
