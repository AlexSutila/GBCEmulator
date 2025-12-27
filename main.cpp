#include "cart/cart.hpp"
#include "gbc.hpp"
#include <stdexcept>

const char *usage = "Usage: ./gbc <rom_path>";

int main(const int argc, const char **argv) {
  if (argc < 2)
    throw std::logic_error(usage);

  // False to run in non-headless mode
  GameBoyColor emulator = GameBoyColor(false);
  cart cart = load_cart_fs(argv[1]);

  emulator.insert_cartridge(cart);
  emulator.run();
  return 0;
}
