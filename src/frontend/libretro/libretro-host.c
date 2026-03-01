#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"

static void fallback_log(enum retro_log_level level, const char *fmt, ...) {}

void retro_init(void) {}

void retro_deinit(void) {}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

void retro_set_controller_port_device(unsigned port, unsigned device) {}

void retro_get_system_info(struct retro_system_info *info) {}

void retro_get_system_av_info(struct retro_system_av_info *info) {}

static struct retro_rumble_interface rumble;

void retro_set_environment(retro_environment_t cb) {}

void retro_set_audio_sample(retro_audio_sample_t cb) {}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {}

void retro_set_input_poll(retro_input_poll_t cb) {}

void retro_set_input_state(retro_input_state_t cb) {}

void retro_set_video_refresh(retro_video_refresh_t cb) {}

void retro_reset(void) {}

void retro_run(void) {}

bool retro_load_game(const struct retro_game_info *info) {}

void retro_unload_game(void) {}

unsigned retro_get_region(void) {}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info,
                             size_t num) {}

size_t retro_serialize_size(void) {}

bool retro_serialize(void *data_, size_t size) {}

bool retro_unserialize(const void *data_, size_t size) {}

void *retro_get_memory_data(unsigned id) {}

size_t retro_get_memory_size(unsigned id) {}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {}
