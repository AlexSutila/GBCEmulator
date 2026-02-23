#include "memory/dma.hpp"
#include "gbc.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "savestate/codec.hpp"
#include <cassert>
#include <optional>
#include <stdexcept>

inline byte_t vdma_bytes_to_blks(const std::size_t bytes) {
  constexpr auto blk_size_bytes = 0x10;
  return (bytes - blk_size_bytes) / blk_size_bytes;
}

inline std::size_t vdma_blks_to_bytes(const byte_t blks) {
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

ObjAttrDMA::DMAState ObjAttrDMA::get_state() const {
  DMAState s{};
  if (state == STATE_OAMDMA_TRAN) {
    s.src_base_address = src_base_addr;
    s.data_offset = data_offset;
    s.active = true;
  } else {
    s.src_base_address = s.data_offset = 0;
    s.active = false;
  }
  return s;
}

DMA::DMA *ObjAttrDMA::get_dma_reg() { return &dma_; }

void ObjAttrDMA::start(const byte_t addr_high) {
  /* The value passed is what is received over the address bus, hence it is only
   * a single byte. This byte determines the upper byte of the source adders. */
  src_base_addr = static_cast<addr_t>(addr_high) * 0x100;
  state = STATE_OAMDMA_INIT;
  clocks_remaining.reset();
  data_offset = 0;
}

void ObjAttrDMA::do_oam_dma_init() {
  static constexpr auto total_clock_cycles = 4 * 2; // T-cycles
  if (!clocks_remaining.has_value())
    clocks_remaining = total_clock_cycles;

  if (clocks_remaining.value() == 4)
    bus_.acquire(BusConflictTypes::BUS_CONFLICT_OAM_DMA);
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
  if (!clocks_remaining.has_value())
    // bus_.acquire(BusConflictTypes::BUS_CONFLICT_OAM_DMA);
    clocks_remaining = total_clock_cycles;

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

enum : std::uint16_t {
  F_OA_SRC_BASE = 1,
  F_OA_DATA_OFFSET,
  F_OA_STATE,
  F_OA_DMA_REG,
  F_OA_CLOCKS_REMAINING,
};

void ObjAttrDMA::savestate_serialize(Savestate::Writer &out) const {
  out.field_u16(F_OA_SRC_BASE, src_base_addr);
  out.field_u16(F_OA_DATA_OFFSET, data_offset);
  out.field_u8(F_OA_STATE, static_cast<byte_t>(state));
  out.field_u8(F_OA_DMA_REG, dma_.peek());
  if (clocks_remaining.has_value())
    out.field_u32(F_OA_CLOCKS_REMAINING,
                  static_cast<std::uint32_t>(clocks_remaining.value()));
}

void ObjAttrDMA::savestate_deserialize(Savestate::Reader &in) {
  clocks_remaining.reset();
  while (const auto field = in.next_field()) {
    auto [id, payload] = *field;
    switch (id) {
    case F_OA_SRC_BASE:
      src_base_addr = payload.u16();
      break;
    case F_OA_DATA_OFFSET:
      data_offset = payload.u16();
      break;
    case F_OA_STATE: {
      const auto raw_state = payload.u8();
      if (raw_state > STATE_OAMDMA_TRAN)
        throw std::runtime_error("ObjAttrDMA::savestate_deserialize()");
      state = static_cast<State>(raw_state);
      break;
    }
    case F_OA_DMA_REG:
      dma_.MMIORegister::write(payload.u8());
      break;
    case F_OA_CLOCKS_REMAINING:
      clocks_remaining = payload.u32();
      break;
    default:
      payload.skip(payload.remaining());
      break;
    }
    payload.expect_eof();
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

VDMA::DMAState VDMA::get_state() const {
  VDMA::DMAState s{};
  if (state == STATE_GDMA_TRAN || state == STATE_HDMA_TRAN) {
    s.gdma_active = (state == STATE_GDMA_TRAN);
    s.hdma_active = (state == STATE_HDMA_TRAN);
    s.dest_base_address = dest_base_addr;
    s.src_base_address = src_base_addr;
    s.data_offset = data_offset;
  } else {
    s.data_offset = s.src_base_address = s.dest_base_address = 0;
    if (state == STATE_HDMA_WAIT)
      s.hdma_waiting = true;
  }
  return s;
}

addr_t VDMA::get_addr(const DMA::VDMA_ADDR &lo, const DMA::VDMA_ADDR &hi) {
  const byte_t hi_byte = hi.get_addr_bits(), lo_byte = lo.get_addr_bits();
  return (static_cast<addr_t>(hi_byte) << 8) | static_cast<addr_t>(lo_byte);
}

void VDMA::set_addr(DMA::VDMA_ADDR &lo, DMA::VDMA_ADDR &hi, const addr_t addr) {
  hi.write(static_cast<byte_t>((addr >> 8) & 0xFF));
  lo.write(static_cast<byte_t>(addr & 0xFF));
}

addr_t VDMA::get_dest_addr() const {
  constexpr addr_t vram_base = 0x8000;
  const addr_t addr_true = get_addr(vdma4_, vdma3_);
  return vram_base | (addr_true & 0x1FF0);
}
void VDMA::set_dest_addr(const addr_t addr) { set_addr(vdma4_, vdma3_, addr); }

addr_t VDMA::get_src_addr() const {
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
   * already performing an ongoing HDMA transfer, then it will be canceled and
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

void VDMA::transfer_byte(const addr_t offset) const {
  byte_t data{0xFF}; // Assume open bus unless address range is sane

  // This is the ideal source address range, read byte as you would expect
  if ((src_base_addr >= 0x0000 && src_base_addr <= 0x7FF0) ||
      (src_base_addr >= 0xA000 && src_base_addr <= 0xDFF0)) [[likely]]
    data = bus_.read_byte(src_base_addr + offset);

  // If the source address lies within this address range, it actually ends up
  // reading from 0xA000-0xBFF0, which is located somewhere in SRAM
  else if (src_base_addr >= 0xE000 && src_base_addr <= 0xFFF0)
    data = bus_.read_byte((src_base_addr - 0x4000) + offset);

  // Only write data byte if the dest address is sane
  if (dest_base_addr >= 0x8000 && dest_base_addr <= 0x9FF0) [[likely]]
    bus_.write_byte(dest_base_addr + offset, data);
}

void VDMA::do_init(const State next_state) {
  // State entry logic
  if (!clocks_remaining.has_value()) {
    constexpr auto total_init_clks = 4;
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
  constexpr auto next_state = STATE_GDMA_TRAN;
  do_init(next_state);
}

void VDMA::do_hdma_init() {
  constexpr auto next_state = STATE_HDMA_TRAN;
  if (!sys_.halted) // HDMA is paused when halted
    do_init(next_state);
}

void VDMA::do_gdma_tran() {
  constexpr auto byte_transfer_clks = 2; // 2 T-cycles

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
  constexpr auto byte_transfer_clks = 2; // 2 T-cycles
  constexpr auto blk_size_bytes = 0x10;  // Fixed transfer size
  if (sys_.halted)                       // HDMA is paused when halted
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

enum : std::uint16_t {
  F_VD_SRC_BASE = 1,
  F_VD_DEST_BASE,
  F_VD_DATA_OFFSET,
  F_VD_TRANSFER_SIZE,
  F_VD_CAN_START_HDMA,
  F_VD_STATE,
  F_VD_VDMA1,
  F_VD_VDMA2,
  F_VD_VDMA3,
  F_VD_VDMA4,
  F_VD_CLOCKS_REMAINING,
};

void VDMA::savestate_serialize(Savestate::Writer &out) const {

  out.field_u16(F_VD_SRC_BASE, src_base_addr);
  out.field_u16(F_VD_DEST_BASE, dest_base_addr);
  out.field_u16(F_VD_DATA_OFFSET, data_offset);
  out.field_u16(F_VD_TRANSFER_SIZE, transfer_size);
  out.field_bool(F_VD_CAN_START_HDMA, can_start_hdma);
  out.field_u8(F_VD_STATE, static_cast<byte_t>(state));
  out.field_u8(F_VD_VDMA1, vdma1_.get_addr_bits());
  out.field_u8(F_VD_VDMA2, vdma2_.get_addr_bits());
  out.field_u8(F_VD_VDMA3, vdma3_.get_addr_bits());
  out.field_u8(F_VD_VDMA4, vdma4_.get_addr_bits());
  if (clocks_remaining.has_value())
    out.field_u32(F_VD_CLOCKS_REMAINING,
                  static_cast<std::uint32_t>(clocks_remaining.value()));
}

void VDMA::savestate_deserialize(Savestate::Reader &in) {
  clocks_remaining.reset();
  while (const auto field = in.next_field()) {
    auto [id, payload] = *field;
    switch (id) {
    case F_VD_SRC_BASE:
      src_base_addr = payload.u16();
      break;
    case F_VD_DEST_BASE:
      dest_base_addr = payload.u16();
      break;
    case F_VD_DATA_OFFSET:
      data_offset = payload.u16();
      break;
    case F_VD_TRANSFER_SIZE:
      transfer_size = payload.u16();
      break;
    case F_VD_CAN_START_HDMA:
      can_start_hdma = payload.boolean();
      break;
    case F_VD_STATE: {
      const auto raw_state = payload.u8();
      if (raw_state > STATE_HDMA_TRAN)
        throw std::runtime_error("VDMA::savestate_deserialize()");
      state = static_cast<State>(raw_state);
      break;
    }
    case F_VD_VDMA1:
      vdma1_.MMIORegister::write(payload.u8());
      break;
    case F_VD_VDMA2:
      vdma2_.MMIORegister::write(payload.u8());
      break;
    case F_VD_VDMA3:
      vdma3_.MMIORegister::write(payload.u8());
      break;
    case F_VD_VDMA4:
      vdma4_.MMIORegister::write(payload.u8());
      break;
    case F_VD_CLOCKS_REMAINING:
      clocks_remaining = payload.u32();
      break;
    default:
      payload.skip(payload.remaining());
      break;
    }
    payload.expect_eof();
  }
}
