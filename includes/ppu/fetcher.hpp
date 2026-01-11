#ifndef __FETCHER_H
#define __FETCHER_H

#include "emu_types.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/fifo.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

struct runtime_sys_info;
class BgPixelFifo;

class BgWinFetcher {
public:
  BgWinFetcher(std::array<std::unique_ptr<byte_t[]>, 2> &vram,
               PPU::LCDCtrl &lcdc,     // The LCD control register
               MMIORegister &scy,      // The scroll Y register
               MMIORegister &scx,      // The scroll X register
               MMIORegister &wy,       // The window Y register
               MMIORegister &wx,       // The window X register
               PPU::LY &ly,            // The current scanline register
               ObjPixelFifo &obj_fifo, // The sprite pixel fifo
               BgPixelFifo &bg_fifo,   // The background pixel fifo
               runtime_sys_info &sys);
  void reset(); // Enters background rendering mode
  void step();

  /* The PPU will signal to clear the FIFO once the rendering of the window has
   * begun. All BG pixel data is flushed, and window rendering starts. */
  bool is_window_visible(byte_t pixels_rendered) const;
  void render_window(); // Makes the pixel begin fetching window tile data

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
  } state{};

  /* Core fetcher logic */
  void do_read_tile();
  void do_read_data_lo();
  void do_read_data_hi();
  void do_push_data();

  /* VRAM tile data and metadata source */
  std::array<std::unique_ptr<byte_t[]>, 2> &vram_;
  byte_t read_vram_byte(addr_t addr, byte_t bank) const;

  /* Internal register references for convenience */
  PPU::LCDCtrl &lcdc_;
  MMIORegister &scy_;
  MMIORegister &scx_;
  MMIORegister &wy_;
  MMIORegister &wx_;
  PPU::LY &ly_;

  /* Internal references to pixel FIFO queues */
  ObjPixelFifo &obj_fifo_;
  BgPixelFifo &bg_fifo_;

  /* Implements fine horizontal scrolling within an 8x8 pixel tile */
  bool should_discard() const;
  byte_t pixels_discarded{};
  byte_t fine_scroll{};

  /* Implements window behavior. If the window is enabled, then it is rendered
   * until the end of the scanline. */
  void reset(bool window_started);
  byte_t win_internal_ly{};
  bool win_started{};

  /* Helpers */
  const byte_t calc_pixel_y() const;
  const byte_t calc_tile_x() const;
  const addr_t calc_tilemap_base() const;
  const byte_t fetch_tile_data(bool high) const;
  const addr_t calc_tile_metadata_addr() const;

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
