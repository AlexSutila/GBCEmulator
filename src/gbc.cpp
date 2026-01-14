#include "gbc.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"
#include "timer/timer.hpp"
#include <memory>

#include "memory/mmio/joypad.hpp"

GameBoyColor::GameBoyColor(Frontend &frontend) : fe_(frontend) {
  /* General system operation info */
  sys_ = {
      .double_speed = false,
      .cgb_mode = true,
      .elapsed_clocks = 0,
  };

  /* Component initializaiton */
  bus = std::make_unique<AddressBus>(sys_);
  cpu = std::make_unique<LR35902>(bus.get(), sys_);
  apu = std::make_unique<APU>(*bus, fe_);
  ppu = std::make_unique<PixelProcessingUnit>(bus.get(), fe_, sys_);
  timer = std::make_unique<TimerUnit>(bus.get(), sys_);
  has_cartridge = false;

  /* Joypad initialization */
  auto *joypad_reg = dynamic_cast<Joypad::JOYP *>(
      bus->get_mmio(IORegisterMapping::MMIO_JOYPAD));
  auto *if_reg = dynamic_cast<InterruptBits *>(
      bus->get_mmio(IORegisterMapping::MMIO_INT_FLAGS));
  if (!joypad_reg || !if_reg)
    throw std::logic_error("Failed to configure joypad MMIO");

  /* To avoid running into problems with other registers it depends on in time,
   * we have to invoke this method to configure the dependencies it needs after
   * we can garuntee they have been instantiated. */
  joypad_reg->set_interrupt_reg(if_reg);
}

void GameBoyColor::insert_cartridge(cart c) {
  if (!bus)
    throw std::logic_error("Bus not initialized");
  bus->insert_cartridge(c);
  has_cartridge = true;
}

void GameBoyColor::init_test_bed() {
  if (!bus)
    throw std::logic_error("Bus not initialized");

  /* Init convenience RAM-only cartridge for testing */
  bus->init_test_bed();
  has_cartridge = true;
}

void GameBoyColor::step() {
  cpu->step();

  // Drives any DMA along that may be currently active
  bus->step_dma();

  // Step remaning components
  ppu->step();
  timer->step();
  apu->step();
  // System clocks are maintained in unit `t-cycles`
  ++sys_.elapsed_clocks;
}
