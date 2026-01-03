#include "gbc.hpp"
#include "cpu/lr35902.hpp"
#include "frontend/renderer.hpp"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"
#include "timer/timer.hpp"
#include <memory>

GameBoyColor::GameBoyColor(bool headless) {
  renderer = std::make_unique<Renderer>(headless);

  /* General system operation info */
  sys = {
      .double_speed = false,
      .cgb_mode = true,
      .elapsed_clocks = 0,
  };

  /* Component initializaiton */
  bus = std::make_unique<AddressBus>(sys);
  cpu = std::make_unique<LR35902>(bus.get(), sys);
  ppu = std::make_unique<PixelProcessingUnit>(bus.get(), renderer.get(), sys);
  timer = std::make_unique<TimerUnit>(bus.get(), sys);
}

void GameBoyColor::insert_cartridge(cart c) {
  if (!bus)
    throw std::logic_error("Bus not initialized");
  bus->insert_cartridge(c);
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
  ++sys.elapsed_clocks;
}

void GameBoyColor::run() {
  while (renderer->get_running()) [[likely]]
    step();
}
