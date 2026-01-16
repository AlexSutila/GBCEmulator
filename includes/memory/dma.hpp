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
    src_base_addr = data_offset = 0;
    clocks_remaining = std::nullopt;
  }
  virtual void step() = 0; // Drives data transfer if active

protected:
  std::optional<std::size_t> clocks_remaining;
  addr_t src_base_addr{}, data_offset{};
  AddressBus &bus_;
};

/*
 * OAM DMA Transfer, applicable to both DMG and CGB
 */
class ObjAttrDMA : public DirectMemoryAccess {
public:
  ObjAttrDMA(AddressBus &bus) : DirectMemoryAccess(bus), dma_(*this) {}
  DMA::DMA *const get_dma_reg();

  void start(const byte_t addr_high); // Begins the actual data transfer
  void step() override;

private:
  /* This is always fixed, although the time required for completion of the data
   * transfer does seem to be impacted by double speed mode. */
  static constexpr auto total_clock_cycles = 160 * 4; // T-cycles
  DMA::DMA dma_;
};

/*
 * VRAM DMA Transfer, applicable to only CGB
 */
class VramDMA : public DirectMemoryAccess {
public:
  VramDMA(AddressBus &bus)
      : DirectMemoryAccess(bus), // To provide bus reading capabilities
        hdma1_(), hdma2_(),      // Source low and high registers
        hdma3_(), hdma4_(),      // Destination low and high registers
        hdma5_(*this)            // The Vram DMA length/mode/start register
  {}
  void step_fast_cycle();
  void step() override;

  /* Getters and setters for both source and destination addresses involve
   * consulting a pair of two 8-bit MMIORegisters to form a 16-bit address. */
  void set_dest_addr(const addr_t addr);
  void set_src_addr(const addr_t addr);
  const addr_t get_dest_addr();
  const addr_t get_src_addr();

private:
  /* See details about these registers under their definitions in `cgb.hpp` */
  MMIORegister hdma1_, hdma2_; // Source low and high registers
  MMIORegister hdma3_, hdma4_; // Destination low and high registers
  DMA::HDMA_MODE_LEN hdma5_;

  void set_addr(MMIORegister &lo, MMIORegister &hi, const addr_t addr);
  const addr_t get_addr(MMIORegister &lo, MMIORegister &hi);
};

#endif //__DMA_H
