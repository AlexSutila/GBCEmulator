#include "ppu/ppu.hpp"
#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"

#include <cassert>
#include <stdexcept>

PixelProcessor::PixelProcessor(AddressBus *bus_ptr) : bus(bus_ptr) {
  using ioregs = IORegisterMapping;

  /* Configure interrupts */
  auto *reg = bus->get_mmio(ioregs::MMIO_INT_ENABLE);
  if (!(ie_reg = dynamic_cast<InterruptBits *>(reg)))
    throw std::logic_error("Failed to connect MMIO_INT_ENABLE");
  reg = bus->get_mmio(ioregs::MMIO_INT_FLAGS);
  if (!(if_reg = dynamic_cast<InterruptBits *>(reg)))
    throw std::logic_error("Failed to connect MMIO_INT_FLAGS");
  assert(ie_reg != nullptr && if_reg != nullptr);
}

void PixelProcessor::step() {
  if_reg->put_vblank(true);
}
