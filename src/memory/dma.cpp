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
  F_OAM_DMA_ACTIVE,
  F_OAM_DMA_DMA_REG,
};

template <typename T> void ObjAttrDMA::parse_savestate(T &t) {
  constexpr auto version = 2; // Schema revision
  t.chunk_header(version, Savestate::C_OAM_DMA);

  t.field_generic(F_OAM_DMA_SRC_BASE, src_base_addr);
  t.field_generic(F_OAM_DMA_DATA_OFFSET, data_offset);
  t.field_generic(F_OAM_DMA_ACTIVE, active);

  // Memory mapped registers
  t.field_complex(F_OAM_DMA_DMA_REG, [&](T &t) { dma_.parse_savestate(t); });
  t.eof();
}

template void ObjAttrDMA::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void ObjAttrDMA::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void ObjAttrDMA::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void ObjAttrDMA::parse_savestate<Savestate::Checker>(Savestate::Checker &);

ObjAttrDMA::ObjAttrDMA(AddressBus &bus, runtime_sys_info &sys, SystemScheduler &g_sched)
    : dma_(*this), sched(g_sched, SchedulerComponent::SCHED_COMPONENT_OAM_DMA,
                         static_cast<std::size_t>(ObjAttrDMA::SchedulerEvent::EVENT_COUNT)),
      bus_(bus), sys_(sys) {
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

void ObjAttrDMA::start(const byte_t addr_high) {
  /* The value passed is what is received over the address bus, hence it is only
   * a single byte. This byte determines the upper byte of the source adders. */
  src_base_addr = static_cast<addr_t>(addr_high) << 8;
  data_offset = 0;

  /* Unschedule any on-going OAM-DMA transfers, in case we start it again while
   * it is already running. */
  for (auto e : {SchedulerEvent::EVENT_COPY_DATA_BYTE, SchedulerEvent::EVENT_ACQUIRE_BUS,
                 SchedulerEvent::EVENT_RELEASE_BUS}) {
    sched.unschedule_event(e);
  }

  // Object attribute DMA runs fast in double speed mode so it must be key1 controlled
  sched.schedule_event_in(clks_key1_controlled(sys_.double_speed, 6),
                          SchedulerEvent::EVENT_ACQUIRE_BUS);
}

ScheduledEventOutcome ObjAttrDMA::handle_event(time_type event_time, unsigned event) {
  constexpr auto total_bytes_to_transfer = 0xA0;

  switch (static_cast<SchedulerEvent>(event)) {
  case SchedulerEvent::EVENT_ACQUIRE_BUS:
    bus_.acquire(BusConflictTypes::BUS_CONFLICT_OAM_DMA);
    sched.schedule_event_on(event_time + clks_key1_controlled(sys_.double_speed, 2),
                            SchedulerEvent::EVENT_COPY_DATA_BYTE);
    active = true;
    break;

  case SchedulerEvent::EVENT_COPY_DATA_BYTE: {
    const addr_t src_addr = src_base_addr + data_offset;
    bus_.get_oam()[data_offset] = bus_.read_byte(src_addr);

    if (++data_offset < total_bytes_to_transfer) [[likely]] {
      sched.schedule_event_on(event_time + clks_key1_controlled(sys_.double_speed, 4),
                              SchedulerEvent::EVENT_COPY_DATA_BYTE);
    } else {
      sched.schedule_event_on(event_time + clks_key1_controlled(sys_.double_speed, 4),
                              SchedulerEvent::EVENT_RELEASE_BUS);
    }
  } break;

  case SchedulerEvent::EVENT_RELEASE_BUS:
    bus_.release(BusConflictTypes::BUS_CONFLICT_OAM_DMA);
    active = false;
    break;

  default:
    __builtin_unreachable();
  }

  return ScheduledEventOutcome::EVENT_OUTCOME_NONE;
}

/* ======================================================================
 * VRAM DMA Transfer, applicable to only CGB
 * ====================================================================== */

enum : std::uint16_t {
  F_VDMA_BYTES_TO_TRANSFER = 1,
  F_VDMA_BYTES_TRANSFERRED,
  F_VDMA_HDMA_PENDING,

  // Memory mapped registers
  F_VDMA_ADDR_REGISTER,
  F_VDMA_MODE_REGISTER,
};

template <typename T> void VDMA::parse_savestate(T &t) {
  constexpr auto version = 2; // Schema revision
  t.chunk_header(version, Savestate::C_VDMA);

  t.field_generic(F_VDMA_BYTES_TO_TRANSFER, bytes_to_transfer);
  t.field_generic(F_VDMA_BYTES_TRANSFERRED, bytes_transferred);
  t.field_generic(F_VDMA_HDMA_PENDING, hdma_pending);

  // duplicate tags here are file, they are all the same thing, just mind ordering
  t.field_complex(F_VDMA_ADDR_REGISTER, [&](T &t) { vdma1_.parse_savestate(t); });
  t.field_complex(F_VDMA_ADDR_REGISTER, [&](T &t) { vdma2_.parse_savestate(t); });
  t.field_complex(F_VDMA_ADDR_REGISTER, [&](T &t) { vdma3_.parse_savestate(t); });
  t.field_complex(F_VDMA_ADDR_REGISTER, [&](T &t) { vdma4_.parse_savestate(t); });
  t.field_complex(F_VDMA_MODE_REGISTER, [&](T &t) { vdma5_.parse_savestate(t); });
  t.eof();
}

template void VDMA::parse_savestate<Savestate::Writer>(Savestate::Writer &);
template void VDMA::parse_savestate<Savestate::Reader>(Savestate::Reader &);
template void VDMA::parse_savestate<Savestate::Sizer>(Savestate::Sizer &);
template void VDMA::parse_savestate<Savestate::Checker>(Savestate::Checker &);

VDMA::VDMA(AddressBus &bus, PixelProcessingUnit &ppu, runtime_sys_info &sys,
           SystemScheduler &g_sched)
    : vdma1_(0xFF), vdma2_(0xF0), vdma3_(0xFF), vdma4_(0xF0), vdma5_(*this),
      sched(g_sched, SchedulerComponent::SCHED_COMPONENT_VRAM_DMA,
            static_cast<std::size_t>(VDMA::SchedulerEvent::EVENT_COUNT)),
      bus_(bus), ppu_(ppu), sys_(sys) {
  bytes_to_transfer = bytes_transferred = 0;
  hdma_pending = false;

  using mmio = IORegisterMapping;
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA1), &vdma1_);
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA2), &vdma2_);
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA3), &vdma3_);
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA4), &vdma4_);
  bus.connect_mmio(static_cast<addr_t>(mmio::MMIO_VDMA5), &vdma5_);

  /* PPU sends start signal to begin HDMA transfer */
  ppu.connect_vdma(this);
}

VDMA::DMAState VDMA::get_state() const {
  VDMA::DMAState s{};
  return s;
}

addr_t VDMA::get_addr(const DMA::VDMA_ADDR &lo, const DMA::VDMA_ADDR &hi) {
  const byte_t hi_byte = hi.get_addr_bits(), lo_byte = lo.get_addr_bits();
  return (static_cast<addr_t>(hi_byte) << 8) | static_cast<addr_t>(lo_byte);
}

void VDMA::inc_addr(DMA::VDMA_ADDR &lo, DMA::VDMA_ADDR &hi) {
  const byte_t hi_byte = hi.get_addr_bits(), lo_byte = lo.get_addr_bits();
  auto addr = (static_cast<addr_t>(hi_byte) << 8) | static_cast<addr_t>(lo_byte);
  ++addr;

  hi.put_addr_bits(static_cast<byte_t>((addr >> 8) & 0xFF));
  lo.put_addr_bits(static_cast<byte_t>(addr & 0xFF));
}

addr_t VDMA::get_dest_addr() const {
  constexpr addr_t vram_base = 0x8000;
  const addr_t addr_true = get_addr(vdma4_, vdma3_);
  return vram_base | (addr_true & 0x1FFF);
}
void VDMA::inc_dest_addr() { inc_addr(vdma4_, vdma3_); }

addr_t VDMA::get_src_addr() const {
  const addr_t addr_true = get_addr(vdma2_, vdma1_);
  return addr_true;
}
void VDMA::inc_src_addr() { inc_addr(vdma2_, vdma1_); }

void VDMA::transfer_byte() {
  const addr_t dest_base_addr = get_dest_addr();
  const addr_t src_base_addr = get_src_addr();
  byte_t data{0xFF}; // Assume open bus unless address range is sane

  // This is the ideal source address range, read byte as you would expect
  if ((src_base_addr >= 0x0000 && src_base_addr <= 0x7FFF) ||
      (src_base_addr >= 0xA000 && src_base_addr <= 0xDFFF)) [[likely]]
    data = bus_.read_byte(src_base_addr);

  // If the source address lies within this address range, it actually ends up
  // reading from 0xA000-0xBFF0, which is located somewhere in SRAM
  else if (src_base_addr >= 0xE000 && src_base_addr <= 0xFFFF)
    data = bus_.read_byte(src_base_addr - 0x4000);

  // Only write data byte if the dest address is sane
  if (dest_base_addr >= 0x8000 && dest_base_addr <= 0x9FFF) [[likely]]
    bus_.write_byte(dest_base_addr, data);

  /* Hardware quirk, docs say the bottom four bits aren't used, but they still exist
   * and increase during VDMA. If you write to FF55, and write to FF55 again after a
   * round of VDMA has completed without updating the source and dest registers, you
   * might not read from the same address both times. */
  bytes_transferred++;
  inc_dest_addr();
  inc_src_addr();
}

void VDMA::try_start(DMA::VDMATransferMode mode, const byte_t blks) {
  bytes_to_transfer = vdma_blks_to_bytes(blks);
  bytes_transferred = 0;

  switch (mode) {
  case DMA::VDMATransferMode::GENERAL_PURPOSE_DMA: {
    const bool hdma_was_active = hdma_pending;
    hdma_pending = false;

    // GDMA is always started when FF55 is written, however should only actually be
    // used when in VBLANK (or I guess HBLANK if you're doing a small transfer).
    if (!hdma_was_active) {
      sched.schedule_event_in(clks_key1_controlled(sys_.double_speed, 4),
                              SchedulerEvent::EVENT_GDMA_COPY_BYTE);
    }
  } break;

  case DMA::VDMATransferMode::HBLANK_DMA: {
    hdma_pending = true;

    // If we start in HBLANK, then HDMA starts instantly
    if (ppu_.get_mode() == PPU::StatModes::MODE_HBLANK)
      try_hdma();
  } break;

  default:
    __builtin_unreachable();
  }
}

// Called by the PPU when entering HBLANK mode (or formally mode 0)
void VDMA::try_hdma() {
  if (!sys_.halted && hdma_active()) {
    sched.schedule_event_in(clks_key1_controlled(sys_.double_speed, 4),
                            SchedulerEvent::EVENT_HDMA_COPY_BYTE);
  }
}

byte_t VDMA::blks_remaining() const {
  const auto bytes_remaining = bytes_to_transfer - bytes_transferred;
  return vdma_bytes_to_blks(bytes_remaining) & 0x7F;
}

bool VDMA::hdma_active() const { return hdma_pending; }

ScheduledEventOutcome VDMA::handle_event(time_type event_time, unsigned event) {
  switch (static_cast<SchedulerEvent>(event)) {
  case SchedulerEvent::EVENT_GDMA_COPY_BYTE: {
    if (!sys_.vdma_active) [[unlikely]]
      sys_.vdma_active = true;
    transfer_byte();

    /* Transfer all blocks of VRAM memory in one swoop */
    if (bytes_transferred < bytes_to_transfer) {
      sched.schedule_event_on(event_time + clks_static_timing(2),
                              SchedulerEvent::EVENT_GDMA_COPY_BYTE);
      return ScheduledEventOutcome::EVENT_OUTCOME_NONE;
    }

    sys_.vdma_active = false;
    return ScheduledEventOutcome::EVENT_OUTCOME_VDMA_COMPLETE;
  }

  case SchedulerEvent::EVENT_HDMA_COPY_BYTE: {
    if (!sys_.vdma_active) [[unlikely]]
      sys_.vdma_active = true;
    transfer_byte();

    /* Transfer one block (16 bytes) of VRAM memory */
    if (bytes_transferred < bytes_to_transfer && bytes_transferred % 0x10 != 0) {
      sched.schedule_event_on(event_time + clks_static_timing(2),
                              SchedulerEvent::EVENT_HDMA_COPY_BYTE);
      return ScheduledEventOutcome::EVENT_OUTCOME_NONE;
    }

    /* Denotes the end of all HDMA transfers, we should not wait for another */
    else if (bytes_transferred == bytes_to_transfer)
      hdma_pending = false;

    sys_.vdma_active = false;
    return ScheduledEventOutcome::EVENT_OUTCOME_VDMA_COMPLETE;
  }

  default:
    __builtin_unreachable();
  }
}
