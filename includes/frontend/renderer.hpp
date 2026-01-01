#ifndef __RENDERER_H
#define __RENDERER_H

#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <memory>

class Renderer {
public:
  Renderer(bool is_headless);
  ~Renderer();

  static constexpr int framebuf_width = 160;
  static constexpr int framebuf_height = 144;
  static constexpr int scale = 4;

  void putPixel(int x, int y, std::uint32_t c);
  void clear();

  bool get_running() const { return running; }
  void poll_events();
  void present();

private:
  std::chrono::time_point<std::chrono::steady_clock> elapsed_time;
  SDL_Renderer *renderer{};
  SDL_Texture *texture{};
  SDL_Window *window{};

  // Frame buffer and rendering control
  std::unique_ptr<std::uint32_t[]> pixels;
  std::uint32_t pixels_rendered{};

  // System keep-alive
  const bool headless{};
  bool running{};
};

#endif // __RENDERER_H
