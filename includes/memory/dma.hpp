#ifndef GBC_DMA_HPP
#define GBC_DMA_HPP

#include "emu_types.hpp"
#include "mmio/cgb.hpp"
#include "mmio/dmg.hpp"
#include "schedule.hpp"
#include <cstddef>

struct runtime_sys_info;
class PixelProcessingUnit;
class AddressBus;

[[nodiscard]] inline byte_t vdma_bytes_to_blks(std::size_t bytes);
[[nodiscard]] inline std::size_t vdma_blks_to_bytes(byte_t blks);

/**
 * OAM DMA Transfer, applicable to both DMG and CGB
 */
class ObjAttrDMA {
public:
  explicit ObjAttrDMA(AddressBus &bus, runtime_sys_info &sys, SystemScheduler &g_sched);
  template <typename T> void parse_savestate(T &t);

  enum class SchedulerEvent : unsigned {
    EVENT_ACQUIRE_BUS = 0,
    EVENT_COPY_DATA_BYTE,
    EVENT_RELEASE_BUS,

    /* For scheduler serialization */
    EVENT_COUNT,
  };

  struct DMAState {
    addr_t src_base_address;
    addr_t data_offset;
    bool active;
  };
  [[nodiscard]] DMAState get_state() const;

  ScheduledEventOutcome handle_event(time_type event_time, unsigned event);
  void start(byte_t addr_high); // Begins the actual data transfer

private:
  addr_t src_base_addr{}, data_offset{};
  DMA::DMA dma_; // MMIO Register

  ChildScheduler sched;
  AddressBus &bus_;

  runtime_sys_info &sys_;
  bool active{false};
};

/**
 * VRAM DMA Transfer, applicable to only CGB
 */
class VDMA {
public:
  explicit VDMA(AddressBus &bus, PixelProcessingUnit &ppu, runtime_sys_info &sys,
                SystemScheduler &g_sched);
  template <typename T> void parse_savestate(T &t);

  enum class SchedulerEvent : unsigned {
    EVENT_GDMA_COPY_BYTE = 0,
    EVENT_HDMA_COPY_BYTE,

    /* For scheduler serialization */
    EVENT_COUNT,
  };

  struct DMAState {
    addr_t dest_base_address;
    addr_t src_base_address;
    addr_t data_offset;
    bool hdma_waiting;
    bool hdma_active;
    bool gdma_active;
  };
  [[nodiscard]] DMAState get_state() const;

  ScheduledEventOutcome handle_event(time_type event_time, unsigned event);
  void try_start(DMA::VDMATransferMode mode, byte_t blks);
  void try_hdma();

  /* Used by register $FF55 (mode/length), to determine the value read. The
   * most significant bit determines if VDMA is active, which will always be
   * a check for HDMA since the CPU cannot ever run during GDMA. Then whats
   * left is just seven bits to denote the blocks which have been copied. */
  byte_t blks_remaining() const;
  bool hdma_active() const;

  /* Getters and setters for both source and destination addresses involve
   * consulting a pair of two 8-bit MMIORegisters to form a 16-bit address. */
  [[nodiscard]] addr_t get_dest_addr() const;
  [[nodiscard]] addr_t get_src_addr() const;
  void inc_dest_addr();
  void inc_src_addr();

private:
  addr_t bytes_to_transfer{}; // How many bytes will be transfered during this VDMA
  addr_t bytes_transferred{}; // How many bytes have been transfered so far
  bool hdma_pending{};        // Are we waiting on an HBLANK VDMA transfer?

  /* See details about these registers under their definitions in `cgb.hpp` */
  DMA::VDMA_ADDR vdma1_, vdma2_; // Source low and high registers
  DMA::VDMA_ADDR vdma3_, vdma4_; // Destination low and high registers
  DMA::VDMA_MODE_LEN vdma5_;

  /* Helpers for working with source and destination address registers. */
  static addr_t get_addr(const DMA::VDMA_ADDR &lo, const DMA::VDMA_ADDR &hi);
  static void inc_addr(DMA::VDMA_ADDR &lo, DMA::VDMA_ADDR &hi);
  void transfer_byte();

  ChildScheduler sched;
  AddressBus &bus_;

  /* Timing metadata */
  PixelProcessingUnit &ppu_;
  runtime_sys_info &sys_;
};

#endif // GBC_DMA_HPP
