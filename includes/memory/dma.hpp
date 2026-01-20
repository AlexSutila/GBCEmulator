#ifndef __DMA_H
#define __DMA_H

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"
#include "mmio/cgb.hpp"
#include "mmio/dmg.hpp"
#include <cstddef>
#include <optional>

struct runtime_sys_info;
class AddressBus;

[[nodiscard]] inline byte_t vdma_bytes_to_blks(std::size_t bytes);
[[nodiscard]] inline std::size_t vdma_blks_to_bytes(byte_t blks);

class DirectMemoryAccess {
public:
  explicit DirectMemoryAccess(AddressBus &bus);
  virtual void step() = 0; // Drives data transfer if active

protected:
  std::optional<std::size_t> clocks_remaining;
  AddressBus &bus_;
};

/*
 * OAM DMA Transfer, applicable to both DMG and CGB
 */
class ObjAttrDMA : public DirectMemoryAccess {
public:
  DMA::DMA *const get_dma_reg();
  explicit ObjAttrDMA(AddressBus &bus);

  void start(const byte_t addr_high); // Begins the actual data transfer
  void step() override;

private:
  addr_t src_base_addr{}, data_offset{};
  /* This is always fixed, although the time required for completion of the data
   * transfer does seem to be impacted by double speed mode. */
  static constexpr auto total_clock_cycles = 160 * 4; // T-cycles
  DMA::DMA dma_;
};

/*
 * VRAM DMA Transfer, applicable to only CGB
 */
class VDMA : public DirectMemoryAccess {
public:
  explicit VDMA(AddressBus &bus, runtime_sys_info &sys);
  MMIORegister *const get_vdma1() { return &vdma1_; }
  MMIORegister *const get_vdma2() { return &vdma2_; }
  MMIORegister *const get_vdma3() { return &vdma3_; }
  MMIORegister *const get_vdma4() { return &vdma4_; }
  MMIORegister *const get_vdma5() { return &vdma5_; }

  /* The initialization phase of DMA is impacted by double speed mode, but the
   * acutal transfer itself is not. Hence, `step_fast_cycle()` exists to run the
   * initial phase to completion twice as fast in double speed mode. */
  void step_fast_cycle();
  void step() override;

  /* For enabling and observing the state of both HDMA and GDMA procedures. */
  bool enabled() const { return gdma_enabled() || hdma_enabled(); }
  void enable(DMA::VDMATransferMode mode, const byte_t blks);

  /* To be used by the VDMA5 register to interrogate the progress of HDMA */
  bool waiting_on_hblank() const { return state == STATE_HDMA_WAIT; }
  bool complete() const { return state == STATE_DISABLED; }
  byte_t get_blks_remaining() const;

  /* For the pixel processor to signal that it is in HBLANK, allowing queued up
   * HDMA transfers to execute. */
  void set_ppu_hblank_signal(bool hblank_enabled);

  /* Getters and setters for both source and destination addresses involve
   * consulting a pair of two 8-bit MMIORegisters to form a 16-bit address. */
  void set_dest_addr(const addr_t addr);
  void set_src_addr(const addr_t addr);
  const addr_t get_dest_addr();
  const addr_t get_src_addr();

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
  bool gdma_enabled() const { return state == STATE_GDMA_TRAN; }
  bool hdma_enabled() const { return state == STATE_HDMA_TRAN; }
  void transfer_byte(const addr_t offset);
  void do_init(State next_state);

  /* See details about these registers under their definitions in `cgb.hpp` */
  MMIORegister vdma1_, vdma2_; // Source low and high registers
  MMIORegister vdma3_, vdma4_; // Destination low and high registers
  DMA::VDMA_MODE_LEN vdma5_;

  /* Core VDMA logic implementation */
  void signal_complete();
  void do_gdma_init();
  void do_gdma_tran();
  void do_hdma_init();
  void do_hdma_tran();
  void do_hdma_wait();

  /* Helpers for working with source and destination address registers. */
  void set_addr(MMIORegister &lo, MMIORegister &hi, const addr_t addr);
  const addr_t get_addr(MMIORegister &lo, MMIORegister &hi);
  runtime_sys_info &sys_;
};

#endif //__DMA_H
