#ifdef __EMSCRIPTEN__

#include "frontend/raylib/frontend.hpp"

int main() {
  cart c = load_cart_fs("/roms/zelda.gbc");
  RaylibFrontend fe(c);
  fe.start();
  return 0;
}

#else

#include "cart/cart.hpp"
#include "frontend/raylib/frontend.hpp"
#include <iostream>

static const char *usage_str = "gbc_simple <rom_path>";

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << usage_str << std::endl;
    return 1;
  }

  // If cartridge is invalid, this will throw. Let OS handle it lol
  cart c = load_cart_fs(argv[1]);
  RaylibFrontend fe(c);
  fe.start();
  return 0;
}

#endif // __EMSCRIPTEN__
