#ifndef __RENDERER_H
#define __RENDERER_H

#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

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
  bool consume_load_request(std::string &rom_path);
  void set_status_message(std::string message);
  void poll_events();
  void present();

private:
  struct UiState {
    bool show_load_window{true};
    bool show_settings_window{false};
    bool request_load{false};
    std::string rom_path{};
    std::string status_message{};
  };

  void build_ui();

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
  UiState ui_state{};
};

#endif // __RENDERER_H
