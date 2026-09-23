#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Open the notification service used for sound output. */
bool audio_init(void);

/** Enable or mute all game audio without changing the selected music cue. */
void audio_set_enabled(bool enabled);

/** Select a looping music cue. Pass -1 to stop. */
void audio_music(int track);

/** Queue a short Celeste sound-effect cue. */
void audio_sfx(int id);

/** Retained for render callback compatibility; worker schedules music. */
void audio_tick(void);

/** Stop sound and release the notification service. */
void audio_deinit(void);

#ifdef __cplusplus
}
#endif
