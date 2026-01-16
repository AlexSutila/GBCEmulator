#include "gbc.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "memory/bus.hpp"
#include "memory/dma.hpp"
#include "memory/mmio/dmg.hpp"
#include "ppu/ppu.hpp"
#include "timer/timer.hpp"
#include <memory>

GameBoyColor::GameBoyColor(Frontend &frontend) : fe_(frontend) {
  /* General system operation info */
  sys_ = {
      .elapsed_clocks = 0,
      .cgb_mode = true,
      .halted = false,
      .speed_switch_armed = false,
      .double_speed = false,
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

void GameBoyColor::step_dma(bool fast_cycle) {
  bus->get_oam_dma().step(); // Runs 2X in double speed

  /* As described elsewhere, HDMA and GDMA have an initialization phase that
   * does run fast in double speed mode, but the transfers themselves don't */
  if (fast_cycle)
    bus->get_vdma().step_fast_cycle();
  else
    bus->get_vdma().step();
}
bool GameBoyColor::vdma_enabled() const { return bus->get_vdma().enabled(); }

void GameBoyColor::step_processor() {
  const auto &vdma = bus->get_vdma();
  if (!vdma.enabled()) // CPU is halted until VDMA is complete
    cpu->step();
}

void GameBoyColor::step() {
  step_processor();
  step_dma(false);
  ppu->step();
  timer->step();
  apu->step();

  // System clocks are maintained in unit `t-cycles`
  ++sys_.elapsed_clocks;

  // If we are in double speed mode, step affected components again
  if (sys_.double_speed) {
    step_processor();
    step_dma(true);
    timer->step();
  }
}
