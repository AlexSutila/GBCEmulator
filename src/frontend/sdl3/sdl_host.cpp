#include "frontend/sdl3/sdl_host.hpp"
#include "SDL3/SDL_render.h"
#include "frontend/logger.hpp"
#include "ppu/palette.hpp"
#include <algorithm>
#include <stdexcept>

SDLHost::SDLHost(const int width, const int height, const int scale) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    throw std::runtime_error(SDL_GetError());

  window =
    SDL_CreateWindow("GBC", width * scale, height * scale + 19 * 2,
                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window)
    throw std::runtime_error(SDL_GetError());

  renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer)
    throw std::runtime_error(SDL_GetError());
  SDL_SetRenderVSync(renderer, 1);
  vsync_enabled = true;

  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!texture)
    throw std::runtime_error(SDL_GetError());
  SDL_SetTextureScaleMode(texture, SDL_ScaleMode::SDL_SCALEMODE_NEAREST);
}

SDLHost::~SDLHost() {
  if (audio_stream) {
    SDL_UnbindAudioStream(audio_stream);
    SDL_DestroyAudioStream(audio_stream);
  }
  if (audio_device)
    SDL_CloseAudioDevice(audio_device);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

/* Rendering */
void SDLHost::update_texture(const std::uint32_t* pixels, const int width,
                             const int height, const std::atomic<bool>& is_cgb,
                             const bool force_mono) const {
  uint32_t* texturePixels{};
  int pitch{};
  SDL_LockTexture(texture, nullptr, reinterpret_cast<void**>(&texturePixels),
                  &pitch);
  pitch /= sizeof(uint32_t);
  const bool cgb = is_cgb.load(std::memory_order_relaxed);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const auto c =
        format_pixel_data(pixels[y * width + x], cgb, force_mono);
      texturePixels[y * pitch + x] = c;
    }
  SDL_UnlockTexture(texture);
}

void SDLHost::draw_texture(const float menu_bar_height,
                           const float bottom_bar_height) const {
  int window_w{}, window_h{};
  SDL_GetWindowSize(window, &window_w, &window_h);
  const auto avail_w = static_cast<float>(window_w);
  const auto avail_h =
    static_cast<float>(window_h) - menu_bar_height - bottom_bar_height;

  // Scale texture so it doesn't warp with window size
  float tex_w{}, tex_h{};
  SDL_GetTextureSize(texture, &tex_w, &tex_h);

  // Fractional scale is fine, as long as its uniform
  const float scale = std::min(avail_w / tex_w, avail_h / tex_h);
  const float dst_w = tex_w * scale;
  const float dst_h = tex_h * scale;

  const SDL_FRect dst_rect{
    (avail_w - dst_w) * 0.5f, // center X
    menu_bar_height +
    (avail_h - dst_h) * 0.5f, // Center Y under menu
    dst_w, dst_h
  };
  SDL_RenderTexture(renderer, texture, nullptr, &dst_rect);
}

std::uint32_t SDLHost::format_pixel_data(const std::uint32_t px,
                                         const bool is_cgb,
                                         const bool force_mono) {
  constexpr std::uint32_t alpha_mask = 0xFF000000;
  /* We are abusing the alpha bits to store DMG color palette indecision CGB
   * mode will always be colored so the bits as are just returned w alpha bits
   * set */

  if (!is_cgb && force_mono) {
    const auto mono_pal_idx = static_cast<byte_t>((px >> 24) & 0xFF);
    return get_mono_color(mono_pal_idx) | alpha_mask;
  }
  return px | alpha_mask;
}

void SDLHost::set_vsync(const bool enabled) {
  if (!renderer) return;
  if (vsync_enabled == enabled) return;
  SDL_SetRenderVSync(renderer, enabled ? 1 : 0);
  vsync_enabled = enabled;
}

/* Audio related */
void SDLHost::init_audio(const int freq, const int channels) {
  SDL_zero(audio_spec);
  audio_spec.freq = freq;
  audio_spec.format = SDL_AUDIO_F32;
  audio_spec.channels = channels;
  audio_device =
    SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec);
  if (!audio_device)
    throw std::runtime_error(SDL_GetError());
  audio_stream = SDL_CreateAudioStream(&audio_spec, &audio_spec);
  if (!audio_stream)
    throw std::runtime_error(SDL_GetError());
  if (!SDL_BindAudioStream(audio_device, audio_stream))
    throw std::runtime_error(SDL_GetError());
}

void SDLHost::queue_audio(const float* samples, const std::size_t count) const {
  if (!samples || count == 0)
    return;

  std::scoped_lock lock(audio_mutex);
  if (!audio_device || !audio_stream)
    return;

  const std::size_t bytes_per_frame =
    static_cast<std::size_t>(audio_spec.channels) * sizeof(float);
  const std::size_t max_queue_bytes =
    static_cast<std::size_t>(audio_spec.freq) * bytes_per_frame; // ~1 second
  const int queued = SDL_GetAudioStreamQueued(audio_stream);

  if (queued < 0) {
    SDL_ClearAudioStream(audio_stream);
    return;
  }
  if (static_cast<std::size_t>(queued) > max_queue_bytes)
    return;

  if (const int byte_count = static_cast<int>(count * sizeof(float));
    !SDL_PutAudioStreamData(audio_stream, samples, byte_count)) {
    SDL_ClearAudioStream(audio_stream);
  }
}

bool SDLHost::set_audio_device(const int device_index,
                               const std::vector<SDL_AudioDeviceID>& ids,
                               const float vol) {
  if (device_index < 0 || device_index >= static_cast<int>(ids.size()))
    return false;

  const SDL_AudioDeviceID desired = ids[device_index];
  std::scoped_lock lock(audio_mutex);
  if (!audio_stream)
    return false;

  // Stop audio on old device
  SDL_ClearAudioStream(audio_stream);
  if (audio_device) {
    SDL_UnbindAudioStream(audio_stream);
    SDL_CloseAudioDevice(audio_device);
    audio_device = 0;
  }

  // Open new device (can be a physical device id or
  // SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
  audio_device = SDL_OpenAudioDevice(desired, &audio_spec);
  if (!audio_device) {
    Logger::push(LogLevel::Error, "Audio", "Failed to open audio device",
                 "Failed to open audio device: " + std::string(SDL_GetError()));
    return false;
  }
  if (!SDL_BindAudioStream(audio_device, audio_stream)) {
    Logger::push(LogLevel::Error, "Audio", "Failed to bind audio stream",
                 "Failed to bind audio stream: " + std::string(SDL_GetError()));
    SDL_CloseAudioDevice(audio_device);
    audio_device = 0;
    return false;
  }

  // Re-apply gain after rebinding
  set_volume(vol);
  return true;
}

void SDLHost::refresh_audio_devices(std::vector<std::string>& names,
                                    std::vector<SDL_AudioDeviceID>& ids) {
  names.clear();
  ids.clear();

  // Slot 0: System default
  ids.push_back(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
  names.emplace_back("System default");

  int count = 0;
  SDL_AudioDeviceID* devs = SDL_GetAudioPlaybackDevices(&count);
  if (!devs) {
    Logger::push(LogLevel::Error, "Audio", "Failed to get audio devices",
                 "Failed to get audio devices: " + std::string(SDL_GetError()));
    return;
  }

  // SDL docs: returns a 0-terminated array; also provides count
  for (int i = 0; devs[i] != 0; ++i) {
    const SDL_AudioDeviceID id = devs[i];
    const char* name = SDL_GetAudioDeviceName(id); // human-readable name
    ids.push_back(id);
    names.emplace_back(name ? name : "(unknown device)");
  }
  SDL_free(devs);
}

void SDLHost::set_volume(const float volume) const {
  std::scoped_lock lock(audio_mutex);
  if (audio_stream) {
    SDL_SetAudioStreamGain(audio_stream, volume);
  }
}

int SDLHost::get_queued_audio_bytes() const {
  std::scoped_lock lock(audio_mutex);
  if (!audio_device || !audio_stream)
    return 0;
  return SDL_GetAudioStreamQueued(audio_stream);
}
