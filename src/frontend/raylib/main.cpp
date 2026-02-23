#include "cart/cart.hpp"
#include "frontend/raylib/frontend.hpp"

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <iostream>
#include <memory>

static std::unique_ptr<RaylibFrontend> g_frontend;

extern "C" {
EMSCRIPTEN_KEEPALIVE void emscripten_start() {
  /* Emscripten uses a virtual filesystem inside the browser? So more or less,
   * the way we handle ROM loading is by copying the rom into the VFS with a
   * hardcoded path, hence we can rely on this naming convention shown here. */
  cart c = load_cart_fs("/rom.bin");
  g_frontend = std::make_unique<RaylibFrontend>(c);
  g_frontend->start();
}

EMSCRIPTEN_KEEPALIVE void emscripten_flush_save() {
  if (g_frontend)
    g_frontend->flush_web_save_now();
}
}

int main() { return 0; }

#else
#include <iostream>

static auto usage_str = "gbc_simple <rom_path>";

int main(const int argc, char **argv) {
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
