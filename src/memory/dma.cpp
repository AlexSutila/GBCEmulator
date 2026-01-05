#include "memory/dma.hpp"
#include "memory/bus.hpp"
#include "memory/mmio/dmg.hpp"
#include <cassert>
#include <optional>

ObjAttrDMA::ObjAttrDMA(AddressBus &bus) : bus_(bus), dma_(*this) {
  src_base_addr = data_offset = 0;
  clocks_remaining = std::nullopt;
}
DMA::DMA *const ObjAttrDMA::get_dma_reg() { return &dma_; }

void ObjAttrDMA::start(const byte_t addr_high) {
  clocks_remaining = total_clock_cycles;
  /* The value passed is what is recieved over the address bus, hence it is only
   * a single byte. This byte determines the upper byte of the source addres. */
  src_base_addr = static_cast<addr_t>(addr_high) * 0x100;
  data_offset = 0;
}

void ObjAttrDMA::step() {
  if (!clocks_remaining.has_value())
    return;

  /* Align data transfer perfectly with the M-cycle clock */
  if (clocks_remaining.value() % 4 == 0) {
    assert(data_offset >= 0 && data_offset <= 0x9F);
    const addr_t src_addr = src_base_addr + data_offset;
    bus_.get_oam()[data_offset] = bus_.read_byte(src_addr);
    ++data_offset;
  }
  --clocks_remaining.value();

  /* Transfer completion logic */
  if (clocks_remaining.value() == 0)
    clocks_remaining.reset();
}
