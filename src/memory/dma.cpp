#include "memory/dma.hpp"
#include "gbc.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include <cassert>
#include <optional>

inline byte_t vdma_bytes_to_blks(std::size_t bytes) {
  constexpr auto blk_size_bytes = 0x10;
  return (bytes - blk_size_bytes) / blk_size_bytes;
}

inline std::size_t vdma_blks_to_bytes(byte_t blks) {
  constexpr auto blk_size_bytes = 0x10;
  return (blks * blk_size_bytes) + blk_size_bytes;
}

/* ======================================================================
 * OAM DMA Transfer, applicable to both DMG and CGB
 * ====================================================================== */

ObjAttrDMA::ObjAttrDMA(AddressBus &bus) : dma_(*this), bus_(bus) {
  src_base_addr = data_offset = 0;
  clocks_remaining.reset();
  state = STATE_DISABLED;
}

DMA::DMA *const ObjAttrDMA::get_dma_reg() { return &dma_; }

void ObjAttrDMA::start(const byte_t addr_high) {
  /* The value passed is what is recieved over the address bus, hence it is only
   * a single byte. This byte determines the upper byte of the source addres. */
  src_base_addr = static_cast<addr_t>(addr_high) * 0x100;
  state = STATE_OAMDMA_INIT;
  clocks_remaining.reset();
  data_offset = 0;
}

void ObjAttrDMA::do_oam_dma_init() {
  static constexpr auto total_clock_cycles = 4; // T-cycles
  if (!clocks_remaining.has_value())
    clocks_remaining = total_clock_cycles;
  --clocks_remaining.value();

  /* State transition logic */
  if (clocks_remaining.value() == 0) {
    state = STATE_OAMDMA_TRAN;
    clocks_remaining.reset();
  }
}

void ObjAttrDMA::do_oam_dma_tran() {
  static constexpr auto total_clock_cycles = 160 * 4; // T-cycles

  /* This is always fixed, although the time required for completion of the data
   * transfer does seem to be impacted by double speed mode. */
  if (!clocks_remaining.has_value()) {
    bus_.acquire(BusConflictTypes::BUS_CONFLICT_OAM_DMA);
    clocks_remaining = total_clock_cycles;
  }

  /* Align data transfer perfectly with the M-cycle clock */
  if (clocks_remaining.value() % 4 == 0) {
    assert(data_offset >= 0 && data_offset <= 0x9F);
    const addr_t src_addr = src_base_addr + data_offset;
    bus_.get_oam()[data_offset] = bus_.read_byte(src_addr);
    ++data_offset;
  }
  --clocks_remaining.value();

  /* Transfer completion logic */
  if (clocks_remaining.value() == 0) {
    bus_.release(BusConflictTypes::BUS_CONFLICT_OAM_DMA);
    clocks_remaining.reset();
    state = STATE_DISABLED;
  }
}

void ObjAttrDMA::step() {
  switch (state) {
  case STATE_OAMDMA_INIT:
    do_oam_dma_init();
    break;
  case STATE_OAMDMA_TRAN:
    do_oam_dma_tran();
    break;
  default:
    break;
  }
}

/* ======================================================================
 * VRAM DMA Transfer, applicable to only CGB
 * ====================================================================== */

VDMA::VDMA(AddressBus &bus, runtime_sys_info &sys)
    : vdma1_(), vdma2_(), // Source low and high registers
      vdma3_(), vdma4_(), // Destination low and high registers
      vdma5_(*this),      // The Vram DMA length/mode/start register
      sys_(sys),          // HDMA is paused in halt mode
      bus_(bus) {
  src_base_addr = dest_base_addr = data_offset = transfer_size = 0;
  state = STATE_DISABLED;
}

const addr_t VDMA::get_addr(MMIORegister &lo, MMIORegister &hi) {
  const byte_t hi_byte = hi.peek(), lo_byte = lo.peek();
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

void VDMA::set_ppu_hblank_signal(bool hblank_enabled) {
  can_start_hdma = hblank_enabled;
}

void VDMA::enable(DMA::VDMATransferMode mode, const byte_t blks) {
  using modes = DMA::VDMATransferMode;
  transfer_size = vdma_blks_to_bytes(blks);
  data_offset = 0;

  /* If the mode bit was written zero, it should start GDMA. However, if we are
   * already performing an ongoing HDMA transfer, then it will be cancelled and
   * no DMA happens. */
  if (mode == modes::GENERAL_PURPOSE_DMA)
    state = (state == STATE_HDMA_WAIT) ? STATE_DISABLED : STATE_GDMA_INIT;

  /* Otherwise, we start an HDMA data transfer. This can only happen if we are
   * not already performing or waiting on HDMA. If the pixel processor already
   * is in HBLANK, then it can begin the transfer right away. Otherwise, must
   * wait until the pixel processor is performing HBLANK. */
  else if (mode == modes::HBLANK_DMA && state == STATE_DISABLED)
    state = (can_start_hdma) ? STATE_HDMA_INIT : STATE_HDMA_WAIT;
}

byte_t VDMA::get_blks_remaining() const {
  const std::size_t bytes_remaining = transfer_size - data_offset;
  return vdma_bytes_to_blks(bytes_remaining);
}

void VDMA::transfer_byte(const addr_t offset) {
  const byte_t data = bus_.read_byte(src_base_addr + offset);
  bus_.write_byte(dest_base_addr + offset, data);
}

void VDMA::do_init(State next_state) {
  constexpr auto total_init_clks = 4 * 4; // 4 M-cycles, 8 T-cycles

  // State entry logic
  if (!clocks_remaining.has_value()) {
    clocks_remaining = total_init_clks;

    // Sample address values and size
    dest_base_addr = get_dest_addr();
    src_base_addr = get_src_addr();
  }
  --clocks_remaining.value();

  // State transition logic
  if (clocks_remaining.value() == 0) {
    clocks_remaining.reset();
    state = next_state;
  }
}

/* In some circumstances, when DMA has completed VDMA5 will return 0xFF when
 * read, meaning we have to update the size to reflect this value. That is all
 * this method is intended to do. */
void VDMA::signal_complete() {
  constexpr auto max_blks = 0x7F;
  transfer_size = vdma_blks_to_bytes(max_blks);
  data_offset = 0;
}

void VDMA::do_gdma_init() {
  const auto next_state = STATE_GDMA_TRAN;
  do_init(next_state);
}

void VDMA::do_hdma_init() {
  const auto next_state = STATE_HDMA_TRAN;
  if (!sys_.halted) // HDMA is paused when halted
    do_init(next_state);
}

void VDMA::do_gdma_tran() {
  constexpr auto byte_transfer_clks = 2 * 4; // 2 M-cycles, 8 T-cycles

  // State entry logic
  if (!clocks_remaining.has_value())
    clocks_remaining = byte_transfer_clks * transfer_size;

  // Data transfer
  if (clocks_remaining.value() % byte_transfer_clks == 0)
    transfer_byte(data_offset++);
  --clocks_remaining.value();

  // State transition logic
  if (clocks_remaining.value() == 0) {
    clocks_remaining.reset();
    state = STATE_DISABLED;
    signal_complete(); // VDMA5 reads 0xFF
  }
}

void VDMA::do_hdma_tran() {
  constexpr auto byte_transfer_clks = 2 * 4; // 2 M-cycles, 8 T-cycles
  constexpr auto blk_size_bytes = 0x10;      // Fixed transfer size
  if (sys_.halted)                           // HDMA is paused when halted
    return;

  // State entry logic, always transfers exactly one block
  if (!clocks_remaining.has_value())
    clocks_remaining = byte_transfer_clks * blk_size_bytes;

  // Data transfer
  if (clocks_remaining.value() % byte_transfer_clks == 0)
    transfer_byte(data_offset++);
  --clocks_remaining.value();

  // State transition logic is trickier here since HDMA does multiple transfers
  if (clocks_remaining.value() != 0)
    return;
  clocks_remaining.reset();

  // If the full transfer is complete, we are done. Otherwise, we have to wait
  // for the PPU to signal that we can begin the transfer of the next data block
  can_start_hdma = false; // Do not rapid fire HDMA transfers
  if (transfer_size != data_offset)
    state = STATE_HDMA_WAIT;
  else {
    state = STATE_DISABLED;
    signal_complete();
  }
}

void VDMA::do_hdma_wait() {
  if (!sys_.halted && can_start_hdma)
    state = STATE_HDMA_INIT;
}

void VDMA::step_fast_cycle() {
  if (state == STATE_GDMA_INIT)
    do_gdma_init();
  else if (state == STATE_HDMA_INIT)
    do_hdma_init();
}

void VDMA::step() {
  switch (state) {
  case STATE_GDMA_INIT:
    do_gdma_init();
    break;
  case STATE_GDMA_TRAN:
    do_gdma_tran();
    break;
  case STATE_HDMA_WAIT:
    do_hdma_wait();
    break;
  case STATE_HDMA_INIT:
    do_hdma_init();
    break;
  case STATE_HDMA_TRAN:
    do_hdma_tran();
    break;
  default:
    break;
  }
}
