#ifndef GBC_DMA_HPP
#define GBC_DMA_HPP

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"
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
  DMA::DMA *get_dma_reg();
  explicit ObjAttrDMA(AddressBus &bus, runtime_sys_info &sys, SystemScheduler &g_sched);
  template <typename T> void parse_savestate(T &t);

  enum SchedulerEvents : unsigned {
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

  void handle_event(time_type event_time, unsigned event);
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
  explicit VDMA(AddressBus &bus, PixelProcessingUnit &ppu, runtime_sys_info &sys);
  template <typename T> void parse_savestate(T &t);

  MMIORegister *get_vdma1() { return &vdma1_; }
  MMIORegister *get_vdma2() { return &vdma2_; }
  MMIORegister *get_vdma3() { return &vdma3_; }
  MMIORegister *get_vdma4() { return &vdma4_; }
  MMIORegister *get_vdma5() { return &vdma5_; }

  struct DMAState {
    addr_t dest_base_address;
    addr_t src_base_address;
    addr_t data_offset;
    bool hdma_waiting;
    bool hdma_active;
    bool gdma_active;
  };
  [[nodiscard]] DMAState get_state() const;

  void try_start(DMA::VDMATransferMode mode, byte_t blks);

  /* Getters and setters for both source and destination addresses involve
   * consulting a pair of two 8-bit MMIORegisters to form a 16-bit address. */
  void set_dest_addr(addr_t addr);
  void set_src_addr(addr_t addr);
  [[nodiscard]] addr_t get_dest_addr() const;
  [[nodiscard]] addr_t get_src_addr() const;

private:
  addr_t src_base_addr{}, dest_base_addr{}, data_offset{}, transfer_size{};

  /* See details about these registers under their definitions in `cgb.hpp` */
  DMA::VDMA_ADDR vdma1_, vdma2_; // Source low and high registers
  DMA::VDMA_ADDR vdma3_, vdma4_; // Destination low and high registers
  DMA::VDMA_MODE_LEN vdma5_;

  /* Helpers for working with source and destination address registers. */
  static void set_addr(DMA::VDMA_ADDR &lo, DMA::VDMA_ADDR &hi, addr_t addr);
  static addr_t get_addr(const DMA::VDMA_ADDR &lo, const DMA::VDMA_ADDR &hi);

  /* Timing metadata */
  runtime_sys_info &sys_;
  PixelProcessingUnit &ppu_;
  AddressBus &bus_;
};

#endif // GBC_DMA_HPP
