#ifndef __RENDERER_H
#define __RENDERER_H

#include "frontend/frontend.hpp"
#include <ImGuiFileDialog.h>
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

class SDL2Frontend final : public Frontend {
public:
  SDL2Frontend();
  ~SDL2Frontend();

  static constexpr int framebuf_width = 160;
  static constexpr int framebuf_height = 144;
  static constexpr int scale = 4;

  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear() override;
  void start() override;

  bool get_running() const { return running.load(); }
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
  const std::uint32_t *front_buffer() const;

  std::chrono::time_point<std::chrono::steady_clock> elapsed_time;
  SDL_Renderer *renderer{};
  SDL_Texture *texture{};
  SDL_Window *window{};

  // Frame buffer and rendering control
  std::array<std::unique_ptr<std::uint32_t[]>, 2> framebuffers;
  std::uint32_t pixels_rendered{};
  std::atomic<int> front_index{0};
  mutable std::mutex ui_mutex{};

  // System keep-alive
  std::atomic<bool> running{};
  UiState ui_state{};

  IGFD::FileDialogConfig config;
  ImVec2 max_size, min_size;
};

#endif // __RENDERER_H
