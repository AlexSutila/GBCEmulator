#ifndef __DMA_H
#define __DMA_H

#include "emu_types.hpp"
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
  VramDMA(AddressBus &bus);

private:
  /* See details about these registers under their definitions in `cgb.hpp` */
  // TODO: HDMA io-registers
};

#endif //__DMA_H
