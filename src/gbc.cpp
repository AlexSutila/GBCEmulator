#include <gbc.hpp>
#include <memory/bus.hpp>
#include <memory>

GameBoyColor::GameBoyColor() {

  /* Main Address Bus */
  bus = std::make_unique<AddressBus>();

  /* GBC Central Processing Unit */
  cpu = std::make_unique<LR35902>(bus.get());
}
