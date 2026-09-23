#pragma once
#include "vendor/celeste.h"
#include <stddef.h>
#include <stdint.h>
extern uint8_t render_frame[8192];
extern uint8_t render_buttons;
extern int render_music;
void render_init(void);
int render_callback(CELESTE_P8_CALLBACK_TYPE call, ...);
void render_mono(uint8_t output[1024], int mode);
