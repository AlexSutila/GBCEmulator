#ifndef __DMA_H
#define __DMA_H

#include "emu_types.hpp"
#include "mmio/dmg.hpp"
#include <cstddef>
#include <optional>

class AddressBus;

/*
 * OAM DMA Transfer, applicable to both DMG and CGB
 */
class ObjAttrDMA {
public:
  ObjAttrDMA(AddressBus &bus);
  DMA::DMA *const get_dma_reg();

  void start(const byte_t addr_high); // Begins the actual data transfer
  void step();                        // Drives data transfer if active

private:
  std::optional<std::size_t> clocks_remaining;
  addr_t src_base_addr, data_offset;

  /* This is always fixed, although the time required for completion of the data
   * transfer does seem to be impacted by double speed mode. */
  static constexpr auto total_clock_cycles = 160 * 4; // T-cycles

  AddressBus &bus_;
  DMA::DMA dma_;
};

#endif //__DMA_H
