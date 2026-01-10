#ifndef __RENDERER_H
#define __RENDERER_H

#include "frontend/frontend.hpp"
#include <ImGuiFileDialog.h>
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>

class SDL3Frontend final : public Frontend {
public:
  SDL3Frontend();
  ~SDL3Frontend();

  static constexpr int framebuf_width = 160;
  static constexpr int framebuf_height = 144;
  static constexpr int scale = 4;

  void put_pixel(int x, int y, std::uint32_t c) override;
  void clear(std::uint32_t c = 0xFFFFFFFF) override;
  void start() override;

  bool consume_load_request(std::string &rom_path);
  void set_status_message(std::string message);
  void poll_events();
  void present_ui();

private:
  void emulation_thread_fn(std::stop_token st, cart c);
  std::jthread emulation_thread;

  // SDL3 display boilerplate
  SDL_Renderer *renderer{};
  SDL_Texture *texture{};
  SDL_Window *window{};

  struct UiState {
    bool show_load_window{true};
    bool show_settings_window{false};
    bool request_load{false};
    std::string rom_path{};
    std::string status_message{};
  };
  const std::uint32_t *front_buffer() const;
  void build_ui();

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
