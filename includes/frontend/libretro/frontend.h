#ifndef GBC_LIBRETRO_FRONTEND_H
#define GBC_LIBRETRO_FRONTEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "libretro.h"
#include <stddef.h>

void irogb_retro_init(void);
void irogb_retro_deinit(void);

void iorgb_retro_set_environment(retro_environment_t cb);
void iorgb_retro_set_audio_sample(retro_audio_sample_t cb);
void iorgb_retro_set_audio_sample_batch(retro_audio_sample_batch_t cb);
void iorgb_retro_set_input_poll(retro_input_poll_t cb);
void iorgb_retro_set_input_state(retro_input_state_t cb);
void iorgb_retro_set_video_refresh(retro_video_refresh_t cb);

bool irogb_retro_load_game(const void *data, size_t size);

void irogb_retro_run(void);

#ifdef __cplusplus
}
#endif

#endif // GBC_LIBRETRO_FRONTEND_H
