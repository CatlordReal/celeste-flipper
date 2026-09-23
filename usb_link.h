#pragma once
#include <stdbool.h>
#include <stdint.h>
bool usb_link_start(void);
void usb_link_stop(void);
uint8_t usb_link_buttons(void);
bool usb_link_pause(void);
void usb_link_frame(const uint8_t *packed_frame);
