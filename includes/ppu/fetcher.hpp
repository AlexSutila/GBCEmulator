#ifndef __FETCHER_H
#define __FETCHER_H

#include "emu_types.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/fifo.hpp"
#include "ppu/sprites.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

struct runtime_sys_info;

class Fetcher {
public:
  Fetcher(std::array<std::unique_ptr<byte_t[]>, 2> &vram,
          PPU::LCDCtrl &lcdc,     // The LCD control register
          MMIORegister &scy,      // The scroll Y register
          MMIORegister &scx,      // The scroll X register
          MMIORegister &wy,       // The window Y register
          MMIORegister &wx,       // The window X register
          PPU::OPRI &opri,        // The CGB object priority register
          PPU::LY &ly,            // The current scanline register
          ObjPixelFifo &obj_fifo, // The sprite pixel fifo
          BgPixelFifo &bg_fifo,   // The background pixel fifo
          runtime_sys_info &sys);
  void reset(); // Enters background rendering mode

  /* Sprite fetching is tricky. It should take priority over both BG and window
   * data fetches, but only occurs on demand. Since the two FIFOs share a single
   * fetcher, sprite fetches will not occur until any ongoing BG or window fetch
   * has run until completion. All this we try to portray accurately. */
  bool step_and_try_sprite_fetch(const Sprite &sprite); // Do sprite fetch asap
  void step(); // Ignores sprites, followes BG/WIN fetch procedures only

  /* The PPU will signal to clear the FIFO once the rendering of the window has
   * begun. All BG pixel data is flushed, and window rendering starts. */
  bool is_window_visible(byte_t pixels_rendered) const;
  void sample_window_enable(); // Window enable bit is sampled at end of mode 2
  void render_window(); // Makes the fetcher begin fetching window tile data

  /* Lastly, the window is kind of strange in that it does not use the curernt
   * scanline register (LY) in the decision to fetch window tiles. It uses an
   * internal counter that only increments if the window was enabled. */
  bool was_window_visible() const { return win_started; }
  void inc_win_ly() { ++win_internal_ly; }
  void reset_win_ly() { win_internal_ly = 0; } // Reset end of every frame

private:
  enum FetcherState {
    STATE_READ_TILE,
    STATE_READ_DATA_LO,
    STATE_READ_DATA_HI,
    STATE_PUSH_DATA,
    // Sprite fetch, only enterable from `step_and_try_sprite_fetch()`
    STATE_SPRITE_FETCH,
  } state{};

  /* Core fetcher logic */
  void do_read_tile();
  void do_read_data_lo();
  void do_read_data_hi();
  void do_push_data();

  /* For sprite fetching specifically, returns true when completed */
  bool do_sprite_fetch(const Sprite &sprite);

  /* VRAM tile data and metadata source */
  std::array<std::unique_ptr<byte_t[]>, 2> &vram_;
  byte_t read_vram_byte(addr_t addr, byte_t bank) const;

  /* Internal register references for convenience */
  PPU::LCDCtrl &lcdc_;
  MMIORegister &scy_;
  MMIORegister &scx_;
  MMIORegister &wy_;
  MMIORegister &wx_;
  PPU::OPRI &opri_;
  PPU::LY &ly_;

  /* Internal references to pixel FIFO queues */
  ObjPixelFifo &obj_fifo_;
  BgPixelFifo &bg_fifo_;

  /* Implements fine horizontal scrolling within an 8x8 pixel tile */
  bool should_discard() const;
  byte_t pixels_discarded{};
  byte_t coarse_scroll_x{};
  byte_t fine_scroll_x{};
  byte_t fine_scroll_y{};

  /* Implements window behavior. If the window is enabled, then it is rendered
   * until the end of the scanline. */
  void reset(bool window_started);
  byte_t win_internal_ly{};
  bool win_enable_sample{};
  bool win_started{};

  /* Helpers */
  const byte_t calc_bgwin_pixel_y() const;
  const byte_t calc_bgwin_tile_x() const;
  const byte_t calc_obj_pixel_y(const Sprite &sprite) const;
  const addr_t calc_tilemap_base() const;
  const byte_t fetch_bgwin_tile_data(bool high) const;
  const byte_t fetch_obj_tile_data(const Sprite &sprite, bool high) const;
  const addr_t calc_tile_metadata_addr() const;
  const byte_t calc_sprite_tile_idx(const Sprite &sprite, bool flip) const;
  const bool has_priority(const pixel &old_px, byte_t new_oam_idx,
                    byte_t new_color_idx) const;

  /* Internal storage that is built up throughout the pixel pushing pipeline.
   * Tile indices are read from memory, data is fetched, and the final data
   * is pushed into the fifo once enough space is free. */
  struct {
    std::size_t tile_idx{};
    byte_t tile_attr{};   // Tile attributes (CGB mode only)
    byte_t data_lo{};     // Low bits of pixel indices
    byte_t data_hi{};     // High bits of pixel indices
    std::size_t x_coor{}; // In unit tiles
  } data;

  /* Internal timing metadata */
  std::optional<std::size_t> total_clks{};
  std::size_t cur_clks{};

  /* Need to distinguish between DMG and CGB */
  runtime_sys_info &sys_;
};

#endif // __FETCHER_H
