#include "gbc.hpp"
#include "cpu/lr35902.hpp"
#include "frontend/renderer.hpp"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"
#include "timer/timer.hpp"
#include <memory>

GameBoyColor::GameBoyColor(bool headless) {
  renderer = std::make_unique<Renderer>(headless);
  bus = std::make_unique<AddressBus>();
  cpu = std::make_unique<LR35902>(bus.get());
  ppu = std::make_unique<PixelProcessingUnit>(bus.get(), renderer.get());
  timer = std::make_unique<TimerUnit>(bus.get());
  elapsed_clocks_ = 0;
}

void GameBoyColor::insert_cartridge(cart c) {
  if (!bus)
    throw std::logic_error("Bus not initialized");
  bus->insert_cartridge(c);

  /* Non-CGB cartridges place the hardware in backwards compatability mode.
   * Inform the components that need to be informed that backwards compatability
   * is enabled. */
  ppu->set_cgb(c.header.cgb_flag());
}

void GameBoyColor::init_test_bed() {
  if (!bus)
    throw std::logic_error("Bus not initialized");

  /* Init convenience RAM-only cartridge for testing */
  bus->init_test_bed();
}

void GameBoyColor::step() {
  cpu->step();
  ppu->step();
  timer->step();
  ++elapsed_clocks_;
}

void GameBoyColor::run() {
  while (renderer->get_running()) [[likely]]
    step();
}
