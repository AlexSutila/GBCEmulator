#include "ppu/ppu.hpp"
#include "cpu/interrupts.hpp"
#include "memory/bus.hpp"
#include "memory/mmio.hpp"
#include "ppu/status.hpp"

#include <cassert>
#include <stdexcept>

template <typename T> T *init_mmio(AddressBus *bus, IORegisterMapping reg_id) {
  auto *reg = bus->get_mmio(reg_id);
  if (auto *casted = dynamic_cast<T *>(reg))
    return casted;
  throw std::logic_error(std::string("Failed to configure MMIO (PPU)"));
}

PixelProcessor::PixelProcessor(AddressBus *bus_ptr) : bus(bus_ptr) {
  using mmio = IORegisterMapping;
  using namespace PPU;

  /* Configure interrupts */
  ie_reg = init_mmio<InterruptBits>(bus, mmio::MMIO_INT_ENABLE);
  if_reg = init_mmio<InterruptBits>(bus, mmio::MMIO_INT_FLAGS);

  /* Initialize status MMIO registers */
  stat_reg = init_mmio<STAT>(bus, mmio::MMIO_LCD_STATUS);
  ly_reg = init_mmio<LY>(bus, mmio::MMIO_LCD_Y_COOR);
  /* LYC is just a typical R/W register, so use MMIORegister */
  lyc_reg = init_mmio<MMIORegister>(bus, mmio::MMIO_LCD_Y_COMP);

  /* Configure status MMIO registers initial state */
  stat_reg->set_mode(StatModes::MODE_OAM_SCAN);
  ly_reg->reset(); // Scanline zero
  lyc_reg->write(0x00);
}

void PixelProcessor::step() { if_reg->put_vblank(true); }
