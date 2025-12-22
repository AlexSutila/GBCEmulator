#include "gbc.hpp"
#include "cpu/lr35902.hpp"
#include "memory"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"

GameBoyColor::GameBoyColor() {
  bus = std::make_unique<AddressBus>();
  cpu = std::make_unique<LR35902>(bus.get());
  ppu = std::make_unique<PixelProcessor>(bus.get());
  elapsed_clocks_ = 0;
}

void GameBoyColor::run() {
  bool running = true;

  while (running) [[likely]] {
    cpu->step();
    ppu->step();
    ++elapsed_clocks_;
  }
}
