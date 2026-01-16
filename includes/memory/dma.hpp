#ifndef __DMA_H
#define __DMA_H

#include "emu_types.hpp"
#include "memory/mmio/mmio.hpp"
#include "mmio/cgb.hpp"
#include "mmio/dmg.hpp"
#include <cstddef>
#include <optional>

class AddressBus;

class DirectMemoryAccess {
public:
  DirectMemoryAccess(AddressBus &bus) : bus_(bus) {
    clocks_remaining = std::nullopt;
  }
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
  ObjAttrDMA(AddressBus &bus) : DirectMemoryAccess(bus), dma_(*this) {
    src_base_addr = data_offset = 0;
  }
  DMA::DMA *const get_dma_reg();

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
  VDMA(AddressBus &bus)
      : DirectMemoryAccess(bus), // To provide bus reading capabilities
        vdma1_(), vdma2_(),      // Source low and high registers
        vdma3_(), vdma4_(),      // Destination low and high registers
        vdma5_(*this) {          // The Vram DMA length/mode/start register
    src_base_addr = dest_base_addr = data_offset = transfer_size = 0;
    state = STATE_DISABLED;
  }
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
  void enable(DMA::VDMATransferMode mode);

  /* Getters and setters for both source and destination addresses involve
   * consulting a pair of two 8-bit MMIORegisters to form a 16-bit address. */
  void set_dest_addr(const addr_t addr);
  void set_src_addr(const addr_t addr);
  const addr_t get_dest_addr();
  const addr_t get_src_addr();

private:
  addr_t src_base_addr{}, dest_base_addr{}, data_offset{}, transfer_size{};
  bool gdma_enabled() const { return state == STATE_GDMA_TRAN; }
  bool hdma_enabled() const { return false; } // TODO

  enum State {
    STATE_DISABLED,  // DMA is not active
    STATE_GDMA_INIT, // Four clock cycle initialization phase
    STATE_GDMA_TRAN, // GDMA data transfer
  } state;

  /* See details about these registers under their definitions in `cgb.hpp` */
  MMIORegister vdma1_, vdma2_; // Source low and high registers
  MMIORegister vdma3_, vdma4_; // Destination low and high registers
  DMA::VDMA_MODE_LEN vdma5_;

  /* Core VDMA logic implementation */
  void do_gdma_init(); // Initialization phase
  void do_gdma_tran(); // Data transfer

  /* Helpers for working with source and destination address registers. */
  void set_addr(MMIORegister &lo, MMIORegister &hi, const addr_t addr);
  const addr_t get_addr(MMIORegister &lo, MMIORegister &hi);
};

#endif //__DMA_H
