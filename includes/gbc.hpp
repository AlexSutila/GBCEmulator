#ifndef __GBC_H
#define __GBC_H

#include "cpu/lr35902.hpp"
#include "memory/bus.hpp"
#include <memory>

class GameBoyColor {
public:
  GameBoyColor();
  void run();

  /* Getters mainly for python bindings */
  AddressBus *get_bus() { return bus.get(); };
  LR35902 *get_cpu() { return cpu.get(); };

private:
  std::unique_ptr<AddressBus> bus{};
  std::unique_ptr<LR35902> cpu{};
};

#endif // __GBC_H
