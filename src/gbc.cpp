#include "gbc.hpp"
#include "cpu/lr35902.hpp"
#include "memory"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"

GameBoyColor::GameBoyColor() {
  timer = std::make_unique<Timer::TimerUnit>();
  bus = std::make_unique<AddressBus>(*timer);
  cpu = std::make_unique<LR35902>(bus.get());
  ppu = std::make_unique<PixelProcessor>(bus.get());
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

void GameBoyColor::run() {
  bool running = true;

  while (running) [[likely]] {
    cpu->step();
    ppu->step();
    timer->tick_tcycles(1);
    ++elapsed_clocks_;
  }
}
