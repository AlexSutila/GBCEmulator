#ifndef GBC_SDL_HOST_HPP
#define GBC_SDL_HOST_HPP

#pragma once
#include "common.hpp"
#include <SDL3/SDL.h>
#include <imgui_impl_sdlrenderer3.h>
#include <mutex>

class SDLHost {

public:
  SDLHost(int width, int height, int scale);
  ~SDLHost();

  // Window and rendering
  SDL_Window* get_window() const { return window; }
  SDL_Renderer* get_renderer() const { return renderer; }
  void update_texture(const std::uint32_t *pixels, int width, int height,
    const std::atomic<bool>& is_cgb, bool force_mono) const;
  void draw_texture(float menu_bar_height, float bottom_bar_height) const;
  static std::uint32_t format_pixel_data(std::uint32_t px, bool is_cgb, bool force_mono);
  void draw_overlay(ImDrawData *draw_data) const {ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, renderer);}
  void present() const {SDL_RenderPresent(renderer);}
  void clear_screen() const {SDL_RenderClear(renderer);}

  // Audio
  void init_audio(int freq = 48000, int channels = 2);
  void queue_audio(const float* samples, size_t count) const;
  bool set_audio_device(int device_index, const std::vector<SDL_AudioDeviceID>& ids, float vol);
  static void refresh_audio_devices(std::vector<std::string>& names, std::vector<SDL_AudioDeviceID>& ids);
  void set_volume(float volume) const;
  int get_queued_audio_bytes() const;
  void clear_audio_stream() const {SDL_ClearAudioStream(audio_stream);}
  SDL_AudioSpec get_audio_spec() const { return audio_spec; }

private:
  SDL_Window* window{nullptr};
  SDL_Renderer* renderer{nullptr};
  SDL_Texture* texture{nullptr};

  SDL_AudioDeviceID audio_device{0};
  SDL_AudioStream* audio_stream{nullptr};
  SDL_AudioSpec audio_spec{};
  mutable std::mutex audio_mutex;
};

#endif //GBC_SDL_HOST_HPP