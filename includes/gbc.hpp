#ifndef __GBC_H
#define __GBC_H

#include "cart/cart.hpp"
#include "cpu/lr35902.hpp"
#include "memory/bus.hpp"
#include "ppu/ppu.hpp"

#include <memory>
#include <stdexcept>

class GameBoyColor {
public:
  GameBoyColor();
  void insert_cartridge(cart c) {
    if (!bus)
      throw std::logic_error("Bus not initialized");
    bus->insert_cartridge(c);
  }
  void run();

  /* Getters mainly for python bindings */
  AddressBus *get_bus() { return bus.get(); };
  LR35902 *get_cpu() { return cpu.get(); };
  PixelProcessor *get_ppu() { return ppu.get(); }

private:
  std::unique_ptr<AddressBus> bus{};
  std::unique_ptr<LR35902> cpu{};
  std::unique_ptr<PixelProcessor> ppu{};
};

#endif // __GBC_H
