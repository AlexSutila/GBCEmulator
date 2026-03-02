#include "frontend/libretro/frontend.hpp"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * Start implementation of C-header (exposed directly to libretro)
 * ====================================================================== */
#include "libretro.h" // Critical: Leave inside the `extern "C"` scope

void retro_init(void) {}

void retro_deinit(void) {}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_controller_port_device(unsigned port, unsigned device) {
  auto &meta = LibretroFrontend::get_instance().get_meta();
  if (port < 1)
    meta.controller_device = device;
}

void retro_get_system_info(struct retro_system_info *info) {
  memset(info, 0, sizeof(*info));
  info->library_name = "IroGC";
  info->library_version = "v1";
  info->need_fullpath = false;
  info->valid_extensions = "gb|gbc|zip";
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
  memset(info, 0, sizeof(*info));
  info->timing = (struct retro_system_timing){
      .fps = 60.0,
      .sample_rate = 48000.0,
  };
  info->geometry = (struct retro_game_geometry){
      .base_width = 160,
      .base_height = 144,
      .max_width = 160,
      .max_height = 144,
      .aspect_ratio = 160.0f / 144.0f,
  };
}

void retro_set_environment(retro_environment_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.environ_cb = cb;

  static const retro_controller_description port1[] = {
      {"Game Boy Joypad", RETRO_DEVICE_JOYPAD}, {nullptr, 0}};
  static const retro_controller_info ports[] = {{port1, 1}, {nullptr, 0}};
  callbacks.environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);

  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  callbacks.environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
  auto &callbacks = LibretroFrontend::get_instance().get_callbacks();
  callbacks.video_cb = cb;
}

void retro_reset(void) {}

void retro_run(void) {
  constexpr std::size_t cycles_per_frame = 70224;
  auto &instance = LibretroFrontend::get_instance();
  for (std::size_t i{0}; i < cycles_per_frame; i++)
    instance.get()->step(); // Step one 'frame'

  instance.try_show_frame();
  instance.try_poll_input();
}

bool retro_load_game(const struct retro_game_info *info) {
  if (!info)
    return false;
  const void *const data = info->data;
  const auto size = info->size;

  auto data_ptr = reinterpret_cast<const byte_t *>(data);
  if (!data_ptr || size == 0)
    return false;

  /* Our interface requires a `std::vector()`, construct accordingly */
  std::vector<byte_t> raw(data_ptr, data_ptr + size);
  auto &gbc = LibretroFrontend::get_instance().get();

  try {
    cart c = load_cart_raw(raw);
    gbc->insert_cartridge(c);
  } catch (...) {
    return false;
  }
  return true;
}

void retro_unload_game(void) {}

/* Does not matter, GBC does not rely on such television standards */
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

bool retro_load_game_special(unsigned type, const struct retro_game_info *info,
                             size_t num) {
  return false;
}

size_t retro_serialize_size(void) { return 0; }

bool retro_serialize(void *data_, size_t size) { return false; }

bool retro_unserialize(const void *data_, size_t size) { return false; }

void *retro_get_memory_data(unsigned id) { return NULL; }

size_t retro_get_memory_size(unsigned id) { return 0; }

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {}

#ifdef __cplusplus
}
#endif
