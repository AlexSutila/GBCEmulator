#include "gbc.hpp"
#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "frontend/renderer.hpp"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"
#include "timer/timer.hpp"
#include <memory>
#include <string>

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
  has_cartridge = false;
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

  // System clocks are maintained in unit `t-cycles`
  ++sys.elapsed_clocks;
}

void GameBoyColor::run() {
  while (renderer->get_running()) [[likely]] {
  std::string rom_path;
  if (renderer->consume_load_request(rom_path)) {
    try {
      cart loaded = load_cart_fs(rom_path.c_str());
      insert_cartridge(loaded);
      renderer->set_status_message("ROM loaded.");
    } catch (const std::exception &e) {
      renderer->set_status_message(
          std::string("Failed to load ROM: ") + e.what());
    }
  }

  if (has_cartridge) {
    step();
  } else {
    renderer->present();
  }
}
}
