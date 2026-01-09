#include "cart/cart.hpp"
#include "cart/cart_display.hpp"
#include "gbc.hpp"

#include <iostream>

int main(const int argc, const char **argv) {
  GameBoyColor emulator = GameBoyColor(false);
  if (argc >= 2) {
    cart cart = load_cart_fs(argv[1]);
    std::cerr << describe_cart(cart) << std::endl;
    emulator.insert_cartridge(cart);
  }
  emulator.run();
  return 0;
}
