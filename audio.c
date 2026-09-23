#include "audio.h"

#include <furi.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#define MUSIC_STEP_MS 480U
typedef struct {
    FuriMessageQueue* queue;
    FuriThread* thread;
} AudioState;

static AudioState audio;

static const NotificationMessage music_delay = {
    .type = NotificationMessageTypeDelay,
    .data.delay.length = 410U,
};

typedef enum {
    AudioCommandEnable,
    AudioCommandMusic,
    AudioCommandSfx,
    AudioCommandStop,
} AudioCommandType;

typedef struct {
    AudioCommandType type;
    int value;
} AudioCommand;

/* PICO-8 pitches transposed one octave for the Flipper speaker. */
static const float music_frequencies[64] = {
    32.70f, 34.65f, 36.71f, 38.89f, 41.20f, 43.65f, 46.25f, 49.00f,
    51.91f, 55.00f, 58.27f, 61.74f, 65.41f, 69.30f, 73.42f, 77.78f,
    82.41f, 87.31f, 92.50f, 98.00f, 103.83f, 110.00f, 116.54f, 123.47f,
    130.81f, 138.59f, 146.83f, 155.56f, 164.81f, 174.61f, 185.00f, 196.00f,
    207.65f, 220.00f, 233.08f, 246.94f, 261.63f, 277.18f, 293.66f, 311.13f,
    329.63f, 349.23f, 369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f,
    523.25f, 554.37f, 587.33f, 622.25f, 659.26f, 698.46f, 739.99f, 783.99f,
    830.61f, 880.00f, 932.33f, 987.77f, 1046.50f, 1108.73f, 1174.66f, 1244.51f,
};

static const NotificationSequence music_sequence_rest = {
    &message_sound_off,
    NULL,
};

/*
 * Monophonic, four-step-downsampled arrangement of the original cartridge's
 * __music__ and __sfx__ data. Source: tannewt/celeste.py celeste-original.p8.
 */
static const uint8_t music_title[] = {
    11, 18, 25, 20, 16, 18, 20, 20, 47, 47, 35, 30, 32, 32, 42, 42,
    47, 47, 35, 30, 32, 32, 42, 42, 30, 25, 28, 32, 30, 25, 37, 30,
    35, 37, 44, 42, 35, 37, 30, 32, 47, 47, 35, 30, 32, 32, 42, 42,
    47, 47, 35, 30, 32, 32, 42, 42, 42, 35, 25, 8, 40, 32, 28, 42,
};
static const uint8_t music_climb[] = {
    42, 42, 35, 61, 47, 47, 47, 42, 42, 42, 35, 61, 47, 47, 47, 42,
    37, 37, 37, 37, 30, 30, 30, 30, 37, 37, 37, 37, 30, 30, 30, 30,
    37, 59, 54, 25, 32, 8, 35, 56, 37, 59, 54, 25, 32, 8, 35, 56,
    37, 59, 54, 25, 35, 32, 35, 40, 37, 37, 37, 52, 0, 54, 44, 49,
};
static const uint8_t music_climb_b[] = {
    37, 28, 32, 32, 28, 28, 30, 32, 37, 28, 32, 32, 28, 28, 30, 32,
    37, 28, 32, 32, 28, 28, 30, 32, 32, 25, 28, 25, 28, 30, 32, 32,
    37, 28, 32, 32, 28, 28, 30, 32, 32, 25, 28, 25, 28, 30, 32, 32,
    42, 42, 52, 49, 47, 47, 44, 44, 37, 37, 37, 37, 37, 37, 37, 37,
    32, 32, 32, 32, 32, 32, 32, 32,
};
static const uint8_t music_summit[] = {
    51, 50, 43, 16, 13, 21, 47, 52, 51, 50, 43, 16, 13, 21, 47, 52,
    51, 50, 43, 16, 13, 21, 47, 52, 49, 44, 43, 49, 44, 42, 47, 44,
};
static const uint8_t music_complete[] = {
    61, 61, 59, 59, 54, 54, 54, 52, 54, 54, 56, 52, 59, 59, 52, 52,
};

static const NotificationMessage sfx_delay_short = {
    .type = NotificationMessageTypeDelay,
    .data.delay.length = 28U,
};
static const NotificationMessage sfx_delay_medium = {
    .type = NotificationMessageTypeDelay,
    .data.delay.length = 48U,
};

#define DEFINE_SFX_NOTE(name, freq_hz)                           \
    static const NotificationMessage sfx_note_##name = {        \
        .type = NotificationMessageTypeSoundOn,                 \
        .data.sound = {.frequency = freq_hz, .volume = 0.55f},  \
    }

DEFINE_SFX_NOTE(c3, 130.81f);
DEFINE_SFX_NOTE(g3, 196.00f);
DEFINE_SFX_NOTE(c4, 261.63f);
DEFINE_SFX_NOTE(e4, 329.63f);
DEFINE_SFX_NOTE(g4, 392.00f);
DEFINE_SFX_NOTE(c5, 523.25f);
DEFINE_SFX_NOTE(e5, 659.25f);
DEFINE_SFX_NOTE(g5, 783.99f);
DEFINE_SFX_NOTE(c6, 1046.50f);

static const NotificationSequence sfx_jump = {
    &sfx_note_c5,
    &sfx_delay_short,
    &sfx_note_e5,
    &sfx_delay_short,
    &message_sound_off,
    NULL,
};
static const NotificationSequence sfx_dash = {
    &sfx_note_c6,
    &sfx_delay_short,
    &sfx_note_g5,
    &sfx_delay_short,
    &message_sound_off,
    NULL,
};
static const NotificationSequence sfx_death = {
    &sfx_note_e4,
    &sfx_delay_medium,
    &sfx_note_c4,
    &sfx_delay_medium,
    &sfx_note_g3,
    &sfx_delay_medium,
    &sfx_note_c3,
    &sfx_delay_medium,
    &message_sound_off,
    NULL,
};
static const NotificationSequence sfx_collect = {
    &sfx_note_e5,
    &sfx_delay_short,
    &sfx_note_g5,
    &sfx_delay_short,
    &sfx_note_c6,
    &sfx_delay_medium,
    &message_sound_off,
    NULL,
};
static const NotificationSequence sfx_action = {
    &sfx_note_g4,
    &sfx_delay_short,
    &message_sound_off,
    NULL,
};

static void audio_play_music_note(NotificationApp* notification, uint8_t note) {
    if(note == 0U || note > COUNT_OF(music_frequencies)) {
        notification_message_block(notification, &music_sequence_rest);
        return;
    }

    const NotificationMessage sound = {
        .type = NotificationMessageTypeSoundOn,
        .data.sound = {.frequency = music_frequencies[note - 1U], .volume = 0.32f},
    };
    const NotificationSequence sequence = {&sound, &music_delay, &message_sound_off, NULL};
    notification_message_block(notification, &sequence);
}

static const NotificationSequence* audio_sfx_sequence(int id) {
    switch(id) {
    case 1:
    case 2:
    case 3:
        return &sfx_jump;
    case 9:
    case 15:
    case 16:
        return &sfx_dash;
    case 0:
    case 37:
    case 38:
        return &sfx_death;
    case 13:
    case 14:
    case 23:
    case 51:
    case 54:
    case 55:
        return &sfx_collect;
    default:
        return &sfx_action;
    }
}

static void audio_select_music(
    int track,
    const uint8_t** music,
    size_t* music_length,
    size_t* music_position) {
    *music = NULL;
    *music_length = 0U;
    *music_position = 0U;
    switch(track) {
    case 0:
        *music = music_title;
        *music_length = COUNT_OF(music_title);
        break;
    case 10:
        *music = music_climb;
        *music_length = COUNT_OF(music_climb);
        break;
    case 20:
        *music = music_climb_b;
        *music_length = COUNT_OF(music_climb_b);
        break;
    case 30:
        *music = music_summit;
        *music_length = COUNT_OF(music_summit);
        break;
    case 40:
        *music = music_complete;
        *music_length = COUNT_OF(music_complete);
        break;
    default:
        break;
    }
}

static int32_t audio_worker(void* context) {
    FuriMessageQueue* queue = context;
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    if(!notification) return -1;
    bool enabled = true;
    const uint8_t* music = NULL;
    size_t music_length = 0U;
    size_t music_position = 0U;
    uint32_t next_music_tick = furi_get_tick();

    for(;;) {
        uint32_t wait = FuriWaitForever;
        if(enabled && music && music_length) {
            const uint32_t now = furi_get_tick();
            if((int32_t)(now - next_music_tick) >= 0) {
                next_music_tick = now + furi_ms_to_ticks(MUSIC_STEP_MS);
                audio_play_music_note(notification, music[music_position]);
                music_position = (music_position + 1U) % music_length;
                continue;
            }
            wait = next_music_tick - now;
        }

        AudioCommand command;
        if(furi_message_queue_get(queue, &command, wait) != FuriStatusOk) continue;
        switch(command.type) {
        case AudioCommandEnable:
            enabled = command.value != 0;
            next_music_tick = furi_get_tick();
            if(!enabled) notification_message_block(notification, &sequence_reset_sound);
            break;
        case AudioCommandMusic:
            audio_select_music(command.value, &music, &music_length, &music_position);
            next_music_tick = furi_get_tick();
            if(!music) notification_message_block(notification, &sequence_reset_sound);
            break;
        case AudioCommandSfx:
            if(enabled) notification_message_block(notification, audio_sfx_sequence(command.value));
            break;
        case AudioCommandStop:
            notification_message_block(notification, &sequence_reset_sound);
            furi_record_close(RECORD_NOTIFICATION);
            return 0;
        }
    }
}

static void audio_post(AudioCommandType type, int value) {
    if(!audio.queue) return;
    const AudioCommand command = {.type = type, .value = value};
    if(furi_message_queue_put(audio.queue, &command, 0U) != FuriStatusOk &&
       type != AudioCommandSfx) {
        /* Keep newest mute/music intent; short effects may be dropped. */
        furi_message_queue_reset(audio.queue);
        furi_message_queue_put(audio.queue, &command, 0U);
    }
}

bool audio_init(void) {
    if(audio.thread) return true;
    audio.queue = furi_message_queue_alloc(8U, sizeof(AudioCommand));
    if(!audio.queue) return false;
    audio.thread = furi_thread_alloc_ex("CelesteAudio", 1024U, audio_worker, audio.queue);
    if(!audio.thread) {
        furi_message_queue_free(audio.queue);
        audio.queue = NULL;
        return false;
    }
    furi_thread_start(audio.thread);
    return true;
}

void audio_set_enabled(bool enabled) {
    audio_post(AudioCommandEnable, enabled ? 1 : 0);
}

void audio_music(int track) {
    audio_post(AudioCommandMusic, track);
}

void audio_sfx(int id) {
    audio_post(AudioCommandSfx, id);
}

void audio_tick(void) {
    /* Worker schedules music; retained for existing render callback. */
}

void audio_deinit(void) {
    if(!audio.thread) return;
    furi_message_queue_reset(audio.queue);
    const AudioCommand stop = {.type = AudioCommandStop, .value = 0};
    furi_check(furi_message_queue_put(audio.queue, &stop, 0U) == FuriStatusOk);
    furi_thread_join(audio.thread);
    furi_thread_free(audio.thread);
    furi_message_queue_free(audio.queue);
    audio.thread = NULL;
    audio.queue = NULL;
}
