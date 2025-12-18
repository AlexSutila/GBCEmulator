#ifndef __GBC_H
#define __GBC_H

#include <cpu/lr35902.hpp>
#include <memory/bus.hpp>
#include <memory>

class GameBoyColor {
public:
  GameBoyColor();
  void run();

private:
  std::unique_ptr<AddressBus> bus;
  std::unique_ptr<LR35902> cpu;
};

#endif // __GBC_H
