#ifndef GBC_DMA_HPP
#define GBC_DMA_HPP

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"
#include "mmio/cgb.hpp"
#include "mmio/dmg.hpp"
#include "schedule.hpp"
#include <cstddef>
#include <optional>

struct runtime_sys_info;
class AddressBus;

[[nodiscard]] inline byte_t vdma_bytes_to_blks(std::size_t bytes);
[[nodiscard]] inline std::size_t vdma_blks_to_bytes(byte_t blks);

/*
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

/*
 * VRAM DMA Transfer, applicable to only CGB
 */
class VDMA {
public:
  explicit VDMA(AddressBus &bus, runtime_sys_info &sys);
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

  /* The initialization phase of DMA is impacted by double speed mode, but the
   * actual transfer itself is not. Hence, `step_fast_cycle()` exists to run the
   * initial phase to completion twice as fast in double speed mode. */
  void step_fast_cycle();
  void step();

  /* For enabling and observing the state of both HDMA and GDMA procedures. */
  [[nodiscard]] bool enabled() const { return gdma_enabled() || hdma_enabled(); }
  void enable(DMA::VDMATransferMode mode, byte_t blks);

  /* To be used by the VDMA5 register to interrogate the progress of HDMA */
  [[nodiscard]] bool waiting_on_hblank() const { return state == STATE_HDMA_WAIT; }
  [[nodiscard]] bool complete() const { return state == STATE_DISABLED; }
  [[nodiscard]] byte_t get_blks_remaining() const;

  /* For the pixel processor to signal that it is in HBLANK, allowing queued up
   * HDMA transfers to execute. */
  void set_ppu_hblank_signal(bool hblank_enabled);

  /* Getters and setters for both source and destination addresses involve
   * consulting a pair of two 8-bit MMIORegisters to form a 16-bit address. */
  void set_dest_addr(addr_t addr);
  void set_src_addr(addr_t addr);
  [[nodiscard]] addr_t get_dest_addr() const;
  [[nodiscard]] addr_t get_src_addr() const;

private:
  addr_t src_base_addr{}, dest_base_addr{}, data_offset{}, transfer_size{};
  bool can_start_hdma{}; // True when PPU is in HBLANK

  enum State {
    STATE_DISABLED, // DMA is not active

    /* GDMA - General purpose DMA */
    STATE_GDMA_INIT, // Initialization
    STATE_GDMA_TRAN, // Data Transfer

    /* HDMA - HBLANK DMA */
    STATE_HDMA_WAIT, // HDMA is enabled, but we are waiting for HBLANK
    STATE_HDMA_INIT, // Initialization
    STATE_HDMA_TRAN, // Data Transfer
  } state;

  /* Generic helper methods */
  [[nodiscard]] bool gdma_enabled() const { return state == STATE_GDMA_TRAN; }
  [[nodiscard]] bool hdma_enabled() const { return state == STATE_HDMA_TRAN; }
  void transfer_byte(addr_t offset) const;
  void do_init(State next_state);

  /* See details about these registers under their definitions in `cgb.hpp` */
  DMA::VDMA_ADDR vdma1_, vdma2_; // Source low and high registers
  DMA::VDMA_ADDR vdma3_, vdma4_; // Destination low and high registers
  DMA::VDMA_MODE_LEN vdma5_;

  /* Core VDMA logic implementation */
  void signal_complete();
  void do_gdma_init();
  void do_gdma_tran();
  void do_hdma_init();
  void do_hdma_tran();
  void do_hdma_wait();

  /* Helpers for working with source and destination address registers. */
  static void set_addr(DMA::VDMA_ADDR &lo, DMA::VDMA_ADDR &hi, addr_t addr);
  static addr_t get_addr(const DMA::VDMA_ADDR &lo, const DMA::VDMA_ADDR &hi);

  /* Timing metadata */
  std::optional<std::size_t> clocks_remaining;
  runtime_sys_info &sys_;
  AddressBus &bus_;
};

#endif // GBC_DMA_HPP
