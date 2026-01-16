#include "memory/dma.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include <cassert>
#include <optional>

/* ======================================================================
 * OAM DMA Transfer, applicable to both DMG and CGB
 * ====================================================================== */
DMA::DMA *const ObjAttrDMA::get_dma_reg() { return &dma_; }

void ObjAttrDMA::start(const byte_t addr_high) {
  clocks_remaining = total_clock_cycles;
  /* The value passed is what is recieved over the address bus, hence it is only
   * a single byte. This byte determines the upper byte of the source addres. */
  src_base_addr = static_cast<addr_t>(addr_high) * 0x100;
  data_offset = 0;
}

void ObjAttrDMA::step() {
  if (!clocks_remaining.has_value())
    return;

  /* Align data transfer perfectly with the M-cycle clock */
  if (clocks_remaining.value() % 4 == 0) {
    assert(data_offset >= 0 && data_offset <= 0x9F);
    const addr_t src_addr = src_base_addr + data_offset;
    bus_.get_oam()[data_offset] = bus_.read_byte(src_addr);
    ++data_offset;
  }
  --clocks_remaining.value();

  /* Transfer completion logic */
  if (clocks_remaining.value() == 0)
    clocks_remaining.reset();
}

/* ======================================================================
 * VRAM DMA Transfer, applicable to only CGB
 * ====================================================================== */

const addr_t VDMA::get_addr(MMIORegister &lo, MMIORegister &hi) {
  const byte_t hi_byte = hi.read(), lo_byte = lo.read();
  return (static_cast<addr_t>(hi_byte) << 8) | static_cast<addr_t>(lo_byte);
}

void VDMA::set_addr(MMIORegister &lo, MMIORegister &hi, const addr_t addr) {
  hi.write(static_cast<byte_t>((addr >> 8) & 0xFF));
  lo.write(static_cast<byte_t>(addr & 0xFF));
}

const addr_t VDMA::get_dest_addr() {
  constexpr addr_t vram_base = 0x8000;
  const addr_t addr_true = get_addr(vdma4_, vdma3_);
  return vram_base | (addr_true & 0x1FF0);
}
void VDMA::set_dest_addr(const addr_t addr) { set_addr(vdma4_, vdma3_, addr); }

const addr_t VDMA::get_src_addr() {
  const addr_t addr_true = get_addr(vdma2_, vdma1_);
  return addr_true & 0xFFF0;
}
void VDMA::set_src_addr(const addr_t addr) { set_addr(vdma2_, vdma1_, addr); }

void VDMA::enable(DMA::VDMATransferMode mode) {
  using modes = DMA::VDMATransferMode;
  if (mode == modes::GENERAL_PURPOSE_DMA)
    state = STATE_GDMA_INIT;

  else {
    // TOOD: HDMA
  }
}

void VDMA::do_gdma_init() {
  constexpr auto total_init_clks = 4;

  // State entry logic
  if (!clocks_remaining.has_value()) {
    clocks_remaining = total_init_clks;
    data_offset = 0;

    // Sample address values and size
    dest_base_addr = get_dest_addr();
    src_base_addr = get_src_addr();
    transfer_size = vdma5_.get_size_bytes();
  }
  --clocks_remaining.value();

  // State transition logic
  if (clocks_remaining.value() == 0) {
    clocks_remaining.reset();
    state = STATE_GDMA_TRAN;
  }
}

void VDMA::do_gdma_tran() {
  constexpr auto byte_transfer_clks = 2;

  // State entry logic
  if (!clocks_remaining.has_value())
    clocks_remaining = byte_transfer_clks * transfer_size;
  --clocks_remaining.value();

  // Data transfer
  if (clocks_remaining.value() % byte_transfer_clks == 0) {
    const byte_t data = bus_.read_byte(src_base_addr + data_offset);
    bus_.write_byte(dest_base_addr + data_offset, data);
    ++data_offset;
  }

  // State transition logic
  if (clocks_remaining.value() == 0) {
    clocks_remaining.reset();
    vdma5_.signal_complete();
    state = STATE_DISABLED;
  }
}

void VDMA::step_fast_cycle() {
  if (state == STATE_GDMA_INIT)
    do_gdma_init();
}

void VDMA::step() {
  switch (state) {
  case STATE_GDMA_INIT:
    do_gdma_init();
    break;
  case STATE_GDMA_TRAN:
    do_gdma_tran();
    break;
  default:
    break;
  }
}
