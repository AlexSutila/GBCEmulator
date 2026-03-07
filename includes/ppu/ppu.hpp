#ifndef GBC_PPU_HPP
#define GBC_PPU_HPP

#include "cpu/interrupts.hpp"
#include "debugger/debugger.hpp"
#include "memory/bus.hpp"
#include "memory/dma.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "memory/mmio/mmio.hpp"
#include "ppu/fetcher.hpp"
#include "ppu/fifo.hpp"
#include "ppu/palette.hpp"
#include "ppu/sprites.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

struct runtime_sys_info;
class Frontend;

class PixelProcessingUnit : Debug::Debuggable {
public:
  PixelProcessingUnit(AddressBus *bus, Frontend &fe,
                      std::optional<Debug::Debugger> &debugger,
                      runtime_sys_info &sys);
  template <typename T> void parse_savestate(T &t);
  void reset();
  void step();

  struct PPUState {
    PPU::StatModes state;
    byte_t lcdc;
    byte_t stat;
    byte_t scx;
    byte_t scy;
    byte_t wy;
    byte_t wx;
    byte_t lyc;
    byte_t ly;
    std::size_t dots;
  };
  [[nodiscard]] PPUState get_state() const;

private:
  InterruptBits *if_reg{};
  runtime_sys_info &sys_;
  Frontend &fe_; // To access frame buffer

  /* Maintain access to relevant memory structures so we don't have to rely on
   * AddressBus::read_byte() and AddressBus::write_byte(). */
  std::array<std::unique_ptr<byte_t[]>, 2> &vram;
  std::unique_ptr<byte_t[]> &oam;

  /* PPU outputs nothing when disabled, hence there must be some mechanism to
   * flush the frame buffers once as the LCDC bit is cleared. */
  bool flush_on_disable{};

  /* Convenience references to important PPU mmio registers */
  PPU::LCDCtrl lcdc_{};
  PPU::STAT stat_{};
  MMIORegister lyc_{};

  /* Background and window positional registers */
  MMIORegister scy_{};
  MMIORegister scx_{};
  MMIORegister wy_{};
  MMIORegister wx_{};

  /* For tracking where we currently are in the rendering process */
  std::size_t row_pixels_rendered{}, sprites_fetched{};
  std::optional<std::uint32_t> get_next_pixel(std::size_t px_idx);
  [[nodiscard]] bool next_sprite_visible(std::size_t px_idx) const;

  /* For popping and combining pixel data from both fifos */
  [[nodiscard]] std::uint32_t resolve_px_priority(const pixel &bg_px,
                                    const pixel &obj_px) const;
  std::optional<std::uint32_t> try_fifo_pop();

  /* Tracks the scanline we are currently on, and related hardware bugs */
  bool should_advance_ly();
  bool scanline_153_bug{};
  PPU::LY ly_{};

  /* For tracking locational data for sprites during OAM search */
  std::size_t sprites_searched{};
  std::vector<Sprite> oam_data{};
  /* For the first frame upon the PPU being enabled, the first scanline has
   * strange timings and OAM is 2 cycles short. TODO: This first frame is not
   * actually pushed to the LCD to prevent visual artifacts. */
  bool ppu_enable_oam_bug{};

  /* Coloring and palette configuration */
  [[nodiscard]] std::uint32_t get_bgwin_rgb(const pixel &px) const;
  [[nodiscard]] std::uint32_t get_obj_rgb(const pixel &px) const;
  PPU::DMGPalette bgp_{}, obp0_{}, obp1_{};

  /* CGB mode object priority resolution */
  PPU::OPRI opri_{};

  /* CGB mode only, VRAM direct memory access */
  VDMA &vdma_;

  /* Pixel Processor operation modes */
  void do_disabled();
  void do_oam_scan();
  void do_draw();

  /* Blanking periods */
  void do_hblank();
  void do_vblank();
  bool blank(); // Returns true when blanking period is complete

  /* Interrupt helpers */
  void request_vblank_irq() const {
    if_reg->put_flag(InterruptFlagMask::INT_FLAG_VBLANK, true);
  }
  void request_lcd_irq() const {
    if_reg->put_flag(InterruptFlagMask::INT_FLAG_LCD, true);
  }
  void update_stat(PPU::StatModes new_mode);
  CircularFifo<PPU::StatModes, 4> stat_delay{};
  bool stat_irq_signal_edge{};

  /* Timing and FSM metadata */
  std::optional<std::size_t> total_mode_clks{};
  std::size_t cur_scanline_clks{};
  std::size_t cur_mode_clks{};
  PPU::StatModes state{};

  /* Pixel FIFO renderers */
  std::unique_ptr<Fetcher> fetcher{};
  ObjPixelFifo obj_fifo{};
  BgPixelFifo bg_fifo{};

  /* Color RAM adding RGB555 support for CGB models */
  std::unique_ptr<ColorRam> obj_cram;
  std::unique_ptr<ColorRam> bg_cram;
};

#endif // GBC_PPU_HPP
