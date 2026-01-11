#ifndef __ATTRIBUTES_H
#define __ATTRIBUTES_H

#include "emu_types.hpp"
#include <cstddef>

[[nodiscard]] byte_t do_y_px_flip(const byte_t y_px,  // Offset within tile
                                  const byte_t attr); // Decides flip

/* Extracts the color index from the data bytes based on the index itself
 * and whether or not the tile is flipped */
[[nodiscard]] byte_t calc_color_idx(const byte_t lo_byte,  // Low data byte
                                    const byte_t hi_byte,  // High data byte
                                    std::size_t pixel_idx, // Which pixel?
                                    bool flip);            // Decides flip

/*
 * BG Map Attributes (CGB Mode only)
 *
 * In CGB mode, an additional 32×32 attribute map is stored in VRAM Bank 1.
 * Each byte in Bank 1 corresponds 1:1 with the BG/Window tile-number map
 * in VRAM Bank 0 (e.g., attribute at 1:9800 applies only to the tile entry
 * at 0:9800).
 *
 * Bit layout (bit 7 = MSB, bit 0 = LSB):
 *
 *   7        6        5        4        3        2        1        0
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * |Priority| Y flip | X flip | (IGN)  |    VRAM Bank    |  Color Palette  |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 *
 * Bit 7 – Priority:
 *   0 = Normal BG/Window priority
 *   1 = BG/Window color indices 1–3 are drawn over OBJ, regardless of OBJ
 * priority
 *
 * Bit 6 – Y flip:
 *   0 = Normal
 *   1 = Tile is vertically mirrored
 *
 * Bit 5 – X flip:
 *   0 = Normal
 *   1 = Tile is horizontally mirrored
 *
 * Bit 4 – Ignored by hardware:
 *   Can be written to and read from, but has no effect on rendering
 *
 * Bit 3 – VRAM Bank:
 *   0 = Fetch tile data from VRAM Bank 0
 *   1 = Fetch tile data from VRAM Bank 1
 *
 * Bits 2–0 – Color Palette:
 *   Selects which BG palette (BGP0–BGP7) to use
 *
 * Note:
 *   Attribute bytes apply per map entry, not per tile index value.
 *   For example, if 0:9800 contains tile index $2A, the attribute at
 *   1:9800 affects only that specific map position, not all tiles $2A.
 */
[[nodiscard]] byte_t get_bg_attrib_palette(byte_t attrib);
[[nodiscard]] byte_t get_bg_attrib_bank(byte_t attrib);
[[nodiscard]] bool get_bg_attrib_x_flip(byte_t attrib);
[[nodiscard]] bool get_bg_attrib_y_flip(byte_t attrib);

/*
 * Object (Sprite) Attributes / Flags
 *
 * Bit layout:
 *   Bit:  7        6        5        4        3     2     1     0
 *         Priority Y flip   X flip   DMG pal  Bank        CGB palette
 *
 * Priority:
 *   0 = OBJ is drawn over BG and Window
 *   1 = BG and Window color indices 1–3 are drawn over this OBJ
 *
 * Y flip:
 *   0 = Normal
 *   1 = Entire OBJ is vertically mirrored
 *
 * X flip:
 *   0 = Normal
 *   1 = Entire OBJ is horizontally mirrored
 *
 * DMG palette (Non-CGB mode only):
 *   0 = OBP0
 *   1 = OBP1
 *
 * Bank (CGB mode only):
 *   0 = Fetch tile data from VRAM bank 0
 *   1 = Fetch tile data from VRAM bank 1
 *
 * CGB palette (CGB mode only):
 *   Selects which OBJ palette (OBP0–OBP7) to use
 */
[[nodiscard]] byte_t get_obj_attrib_dmg_palette(byte_t attrib);
[[nodiscard]] byte_t get_obj_attrib_bank(byte_t attrib);
[[nodiscard]] byte_t get_obj_attrib_cgb_palette(byte_t attrib);
[[nodiscard]] bool get_obj_attrib_x_flip(byte_t attrib);
[[nodiscard]] bool get_obj_attrib_y_flip(byte_t attrib);

#endif // __ATTRIBUTES_H
