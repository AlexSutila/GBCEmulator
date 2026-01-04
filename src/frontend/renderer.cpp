#include "frontend/renderer.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>

Renderer::Renderer(bool is_headless) : headless(is_headless) {
  if (!SDL_Init(SDL_INIT_VIDEO))
    throw std::runtime_error(SDL_GetError());

  if (!headless) {
    window = SDL_CreateWindow("GBC", framebuf_width * scale,
                              framebuf_height * scale, SDL_WINDOW_RESIZABLE);
    if (!window)
      throw std::runtime_error(SDL_GetError());

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
      throw std::runtime_error(SDL_GetError());

    /* Enable vsync (SDL3 way) */
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, framebuf_width,
                                framebuf_height);
    if (!texture)
      throw std::runtime_error(SDL_GetError());

    /* For 60hz synchronization */
    elapsed_time = std::chrono::steady_clock::now();
  }

  /* Still allocate frame buffer for snapshots in headless mode */
  pixels = std::make_unique<std::uint32_t[]>(framebuf_height * framebuf_width);
  pixels_rendered = 0;
  running = true;
  clear();
}

Renderer::~Renderer() {
  if (!headless) {
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
  }
}

void Renderer::putPixel(int x, int y, std::uint32_t c) {
  if (x < 0 || x >= framebuf_width || y < 0 || y >= framebuf_height)
    return;
  pixels[y * framebuf_width + x] = c;
  ++pixels_rendered;

  if (pixels_rendered != framebuf_height * framebuf_width)
    return;
  pixels_rendered = 0;

  if (!headless)
    present();
}

void Renderer::poll_events() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT)
      running = false;
  }
}

inline auto calc_delta(const std::chrono::steady_clock::time_point &start) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

void Renderer::present() {
  constexpr float delta = 16666.66667f;
  using namespace std::chrono;

  if (headless)
    return;

  uint32_t *texturePixels;
  int pitch;
  SDL_LockTexture(texture, nullptr, reinterpret_cast<void **>(&texturePixels),
                  &pitch);

  pitch /= sizeof(uint32_t);
  for (int y = 0; y < framebuf_height; ++y)
    for (int x = 0; x < framebuf_width; ++x)
      texturePixels[y * pitch + x] = pixels[y * framebuf_width + x];

  SDL_UnlockTexture(texture);
  SDL_RenderClear(renderer);
  SDL_RenderTexture(renderer, texture, nullptr, nullptr);
  SDL_RenderPresent(renderer);
  poll_events();

  /* Sync to sixty herts */
  while (calc_delta(elapsed_time) < delta)
    ;
  elapsed_time = std::chrono::steady_clock::now();
}

void Renderer::clear() {
  constexpr std::uint32_t white = 0xFFFFFFFF;
  for (int i = 0; i < framebuf_width * framebuf_height; ++i)
    pixels[i] = white;
}
