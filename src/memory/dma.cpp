#include "memory/dma.hpp"
#include "gbc.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/cgb.hpp"
#include "memory/mmio/dmg.hpp"
#include "savestate/codec.hpp"
#include "schedule.hpp"
#include <cassert>

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

enum : std::uint16_t {
  F_OAM_DMA_SRC_BASE = 1,
  F_OAM_DMA_DATA_OFFSET,
  F_OAM_DMA_STATE,
  F_OAM_DMA_CLOCKS_REMAINING,

  // Memory mapped registers
  F_OAM_DMA_DMA_REG,
};

template <typename T> void ObjAttrDMA::parse_savestate(T &t) {
  constexpr auto version = 1; // Schema revision
  t.chunk_header(version, Savestate::C_OAM_DMA);

  t.field_generic(F_OAM_DMA_SRC_BASE, src_base_addr);
  t.field_generic(F_OAM_DMA_DATA_OFFSET, data_offset);

  // Memory mapped registers
  t.field_complex(F_OAM_DMA_DMA_REG, [&](T &t) { dma_.parse_savestate(t); });
  t.eof();
}

template void ObjAttrDMA::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void ObjAttrDMA::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void ObjAttrDMA::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void ObjAttrDMA::parse_savestate<Savestate::Checker>(Savestate::Checker &);

ObjAttrDMA::ObjAttrDMA(AddressBus &bus, runtime_sys_info &sys, SystemScheduler &g_sched)
    : dma_(*this), sched(g_sched, SCHED_COMPONENT_OAM_DMA), bus_(bus), sys_(sys) {
  bus.connect_mmio(static_cast<addr_t>(IORegisterMapping::MMIO_OAM_DMA), &dma_);
  src_base_addr = data_offset = 0;
}

ObjAttrDMA::DMAState ObjAttrDMA::get_state() const {
  DMAState s{
      .src_base_address = src_base_addr,
      .data_offset = data_offset,
      .active = active,
  };
  return s;
}

DMA::DMA *ObjAttrDMA::get_dma_reg() { return &dma_; }

void ObjAttrDMA::start(const byte_t addr_high) {
  /* The value passed is what is received over the address bus, hence it is only
   * a single byte. This byte determines the upper byte of the source adders. */
  src_base_addr = static_cast<addr_t>(addr_high) << 8;
  data_offset = 0;

  /* Unschedule any on-going OAM-DMA transfers, in case we start it again while
   * it is already running. */
  for (auto e : {EVENT_COPY_DATA_BYTE, EVENT_ACQUIRE_BUS, EVENT_RELEASE_BUS})
    sched.unschedule_event(e);
  sched.schedule_event_in(clks_key1_controlled(sys_.double_speed, 6), EVENT_ACQUIRE_BUS);
}

void ObjAttrDMA::handle_event(time_type event_time, unsigned event) {
  constexpr auto total_bytes_to_transfer = 0xA0;

  switch (static_cast<SchedulerEvents>(event)) {
  case EVENT_ACQUIRE_BUS:
    bus_.acquire(BusConflictTypes::BUS_CONFLICT_OAM_DMA);
    sched.schedule_event_on(event_time + clks_key1_controlled(sys_.double_speed, 2),
                            EVENT_COPY_DATA_BYTE);
    active = true;
    break;

  case EVENT_COPY_DATA_BYTE: {
    const addr_t src_addr = src_base_addr + data_offset;
    bus_.get_oam()[data_offset] = bus_.read_byte(src_addr);

    if (++data_offset < total_bytes_to_transfer) [[likely]] {
      sched.schedule_event_on(event_time + clks_key1_controlled(sys_.double_speed, 4),
                              EVENT_COPY_DATA_BYTE);
    } else {
      sched.schedule_event_on(event_time + clks_key1_controlled(sys_.double_speed, 4),
                              EVENT_RELEASE_BUS);
    }
  } break;

  case EVENT_RELEASE_BUS:
    bus_.release(BusConflictTypes::BUS_CONFLICT_OAM_DMA);
    active = false;
    break;

  default:
    break;
  }
}

/* ======================================================================
 * VRAM DMA Transfer, applicable to only CGB
 * ====================================================================== */

enum : std::uint16_t {
  F_VDMA_SRC_BASE = 1,
  F_VDMA_DEST_BASE,
  F_VDMA_DATA_OFFSET,
  F_VDMA_TRANSFER_SIZE,
  F_VDMA_CAN_START_HDMA,
  F_VDMA_STATE,
  F_VDMA_CLOCKS_REMAINING,

  // Memory mapped registers
  F_VDMA_ADDR_REG,
  F_VDMA_MODE_REG,
};

template <typename T> void VDMA::parse_savestate(T &t) {
  constexpr auto version = 1; // Schema revision
  t.chunk_header(version, Savestate::C_VDMA);

  t.field_generic(F_VDMA_SRC_BASE, src_base_addr);
  t.field_generic(F_VDMA_DEST_BASE, dest_base_addr);
  t.field_generic(F_VDMA_DATA_OFFSET, data_offset);
  t.field_generic(F_VDMA_TRANSFER_SIZE, transfer_size);

  // duplicate tags here are fine, they are all the same thing, just mind order
  t.field_complex(F_VDMA_ADDR_REG, [&](T &t) { vdma1_.parse_savestate(t); });
  t.field_complex(F_VDMA_ADDR_REG, [&](T &t) { vdma2_.parse_savestate(t); });
  t.field_complex(F_VDMA_ADDR_REG, [&](T &t) { vdma3_.parse_savestate(t); });
  t.field_complex(F_VDMA_ADDR_REG, [&](T &t) { vdma4_.parse_savestate(t); });
  t.field_complex(F_VDMA_MODE_REG, [&](T &t) { vdma5_.parse_savestate(t); });
  t.eof();
}

template void VDMA::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void VDMA::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void VDMA::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void VDMA::parse_savestate<Savestate::Checker>(Savestate::Checker &);

VDMA::VDMA(AddressBus &bus, PixelProcessingUnit &ppu, runtime_sys_info &sys)
    : vdma1_(), vdma2_(), // Source low and high registers
      vdma3_(), vdma4_(), // Destination low and high registers
      vdma5_(*this),      // The Vram DMA length/mode/start register
      sys_(sys),          // Vram DMA runs fast in double speed mode
      ppu_(ppu),          // HDMA activation depends on PPU state
      bus_(bus) {
  using mmio = IORegisterMapping;
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA1), &vdma1_);
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA2), &vdma2_);
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA3), &vdma3_);
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA4), &vdma4_);
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA5), &vdma5_);
  src_base_addr = dest_base_addr = data_offset = transfer_size = 0;
}

VDMA::DMAState VDMA::get_state() const {
  VDMA::DMAState s{};
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

void VDMA::try_start(DMA::VDMATransferMode mode, const byte_t blks) {
  transfer_size = vdma_blks_to_bytes(blks);
  data_offset = 0;
}
