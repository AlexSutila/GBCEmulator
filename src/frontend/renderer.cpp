#include "frontend/renderer.hpp"
#include "emu_types.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <stdexcept>

static constexpr std::uint32_t PALETTE[4] = {
    0xFFFFFFFF, // white
    0xFFAAAAAA, // light-grey
    0xFF555555, // dark-grey
    0xFF000000  // black
};

Renderer::Renderer() {
  if (!SDL_Init(SDL_INIT_VIDEO))
    throw std::runtime_error(SDL_GetError());
  running = true;

  window = SDL_CreateWindow("GBC", FB_WIDTH * SCALE, FB_HEIGHT * SCALE,
                            SDL_WINDOW_RESIZABLE);
  if (!window)
    throw std::runtime_error(SDL_GetError());

  renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer)
    throw std::runtime_error(SDL_GetError());

  /* Enable vsync (SDL3 way) */
  SDL_SetRenderVSync(renderer, 1);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, FB_WIDTH, FB_HEIGHT);
  if (!texture)
    throw std::runtime_error(SDL_GetError());
  pixels = std::make_unique<std::uint32_t[]>(FB_HEIGHT * FB_WIDTH);
  clear();
}

Renderer::~Renderer() {
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

void Renderer::putPixel(int x, int y, byte_t paletteIndex) {
  if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT)
    return;
  pixels[y * FB_WIDTH + x] = PALETTE[paletteIndex & 0x03];
}

void Renderer::poll_events() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT)
      running = false;
  }
}

void Renderer::present() {
  int pitch;
  uint32_t *texturePixels;

  SDL_LockTexture(texture, nullptr, reinterpret_cast<void **>(&texturePixels),
                  &pitch);

  pitch /= sizeof(uint32_t);
  for (int y = 0; y < FB_HEIGHT; ++y)
    for (int x = 0; x < FB_WIDTH; ++x)
      texturePixels[y * pitch + x] = pixels[y * FB_WIDTH + x];

  SDL_UnlockTexture(texture);
  SDL_RenderClear(renderer);
  SDL_RenderTexture(renderer, texture, nullptr, nullptr);
  SDL_RenderPresent(renderer);
  poll_events();
}

void Renderer::clear() {
  for (int i = 0; i < FB_WIDTH * FB_HEIGHT; ++i)
    pixels[i] = 0xFFFFFFFF;
}
