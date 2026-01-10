#include "frontend/sdl3_frontend.hpp"

int main(const int argc, const char **argv) {
  auto emulator = SDL3Frontend();
  emulator.start();
  return 0;
}
