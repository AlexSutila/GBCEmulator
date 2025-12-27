#ifndef __RENDERER_H
#define __RENDERER_H

#include "emu_types.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <memory>

class Renderer {
public:
  Renderer();
  ~Renderer();

  static constexpr int FB_WIDTH = 160;
  static constexpr int FB_HEIGHT = 144;
  static constexpr int SCALE = 4;

  void putPixel(int x, int y, byte_t paletteIndex);
  bool get_running() const { return running; }
  void poll_events();
  void present();
  void clear();

private:
  std::chrono::time_point<std::chrono::steady_clock> elapsed_time;
  std::unique_ptr<std::uint32_t[]> pixels;
  SDL_Window *window{};
  SDL_Renderer *renderer{};
  SDL_Texture *texture{};

  // System keep-alive
  bool running{};
};

#endif // __RENDERER_H
