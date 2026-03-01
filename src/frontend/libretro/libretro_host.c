#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/libretro/frontend.h"
#include "libretro.h"

void retro_init(void) { irogb_retro_init(); }

void retro_deinit(void) { irogb_retro_deinit(); }

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_controller_port_device(unsigned port, unsigned device) {}

void retro_get_system_info(struct retro_system_info *info) {
  memset(info, 0, sizeof(*info));
  info->library_name = "IroGC";
  info->library_version = "v1";
  info->need_fullpath = false;
  info->valid_extensions = NULL;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
  info->timing.fps = 60.0;
  info->timing.sample_rate = 44100.0;

  info->geometry.base_height = 144;
  info->geometry.base_width = 160;
  info->geometry.max_height = 144;
  info->geometry.max_width = 160;
  info->geometry.aspect_ratio = 160.0f / 144.0f;
}

void retro_set_environment(retro_environment_t cb) {
  iorgb_retro_set_environment(cb);
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
  iorgb_retro_set_audio_sample(cb);
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  iorgb_retro_set_audio_sample_batch(cb);
}

void retro_set_input_poll(retro_input_poll_t cb) {
  iorgb_retro_set_input_poll(cb);
}

void retro_set_input_state(retro_input_state_t cb) {
  iorgb_retro_set_input_state(cb);
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
  iorgb_retro_set_video_refresh(cb);
}

void retro_reset(void) {}

void retro_run(void) { irogb_retro_run(); }

bool retro_load_game(const struct retro_game_info *info) {
  if (!info)
    return false;
  return irogb_retro_load_game(info->data, info->size);
}

void retro_unload_game(void) {}

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
