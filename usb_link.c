#include "usb_link.h"
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_cdc.h>
#include <storage/storage.h>
#include <string.h>
#define RX 1u
#define TX 2u
#define STOP 4u
#define FRAME 8u
#define DISCONNECT 16u
#define EXE_PATH EXT_PATH("apps_data/celeste_classic/Celeste-Windows.exe")
typedef struct {
  FuriThread *thread;
  FuriMutex *lock;
  FuriStreamBuffer *rx;
  FuriHalUsbInterface *previous;
  uint8_t frame[8196];
  uint8_t keys;
  uint32_t last_keys;
  bool pause, request, ready;
} Link;
static Link *link;
static void tx(void *ctx) {
  Link *l = ctx;
  furi_thread_flags_set(furi_thread_get_id(l->thread), TX);
}
static void rx(void *ctx) {
  Link *l = ctx;
  uint8_t b[64];
  int n = furi_hal_cdc_receive(1, b, 64);
  if (n > 0)
    furi_stream_buffer_send(l->rx, b, n, 0);
  furi_thread_flags_set(furi_thread_get_id(l->thread), RX);
}
static void changed(void *ctx, CdcState s) {
  Link *l = ctx;
  if (s == CdcStateDisconnected)
    furi_thread_flags_set(furi_thread_get_id(l->thread), DISCONNECT);
}
static void control(void *ctx, CdcCtrlLine c) {
  Link *l = ctx;
  if (!(c & CdcCtrlLineDTR))
    furi_thread_flags_set(furi_thread_get_id(l->thread), DISCONNECT);
}
static CdcCallbacks callbacks = {.tx_ep_callback = tx,
                                 .rx_ep_callback = rx,
                                 .state_callback = changed,
                                 .ctrl_line_callback = control};
static void reset(Link *l) {
  furi_mutex_acquire(l->lock, FuriWaitForever);
  l->ready = l->request = false;
  l->keys = 0;
  furi_mutex_release(l->lock);
  furi_stream_buffer_reset(l->rx);
}
static int32_t worker(void *context) {
  Link *l = context;
  size_t offset = 0, packet_size = 0;
  bool sending = false;
  uint32_t sent_at = 0;
  uint8_t state = 0, key = 0, packet[64];
  Storage *storage = furi_record_open(RECORD_STORAGE);
  File *file = storage_file_alloc(storage);
  bool executable = false;
  uint32_t remaining = 0;
  for (;;) {
    uint32_t flags = furi_thread_flags_wait(RX | TX | STOP | FRAME | DISCONNECT,
                                            FuriFlagWaitAny, 20);
    if (!(flags & FuriFlagError) && (flags & STOP))
      break;
    if (!(flags & FuriFlagError) && (flags & DISCONNECT)) {
      reset(l);
      sending = false;
      offset = 0;
      state = 0;
      executable = false;
      storage_file_close(file);
      continue;
    }
    bool start_exe = false;
    uint8_t c;
    while (furi_stream_buffer_receive(l->rx, &c, 1, 0) == 1) {
      furi_mutex_acquire(l->lock, FuriWaitForever);
      if (state == 1) {
        key = c;
        state = 2;
      } else if (state == 2) {
        if (c == '\n') {
          l->keys = key & 63;
          l->last_keys = furi_get_tick();
        }
        state = 0;
      } else if (state == 3) {
        state = c == 'X' ? 4 : 0;
      } else if (state == 4) {
        state = c == 'E' ? 5 : 0;
      } else if (state == 5) {
        start_exe = c == '\n';
        state = 0;
      } else if (c == 'K')
        state = 1;
      else if (c == 'F')
        l->request = true;
      else if (c == 'P')
        l->pause = true;
      else if (c == 'E')
        state = 3;
      furi_mutex_release(l->lock);
    }
    if (sending && !(flags & FuriFlagError) && (flags & TX)) {
      if (!executable)
        offset += packet_size;
      sending = false;
    }
    if (sending && furi_get_tick() - sent_at > furi_ms_to_ticks(1000)) {
      /* Cancel endpoint before reusing its transfer buffer. Host reconnects. */
      furi_hal_cdc_set_callbacks(1, NULL, NULL);
      furi_hal_usb_set_config(NULL, NULL);
      furi_hal_usb_set_config(&usb_cdc_dual, NULL);
      furi_hal_cdc_set_callbacks(1, &callbacks, l);
      reset(l);
      sending = false;
      offset = 0;
      state = 0;
      executable = false;
      storage_file_close(file);
      continue;
    }
    if (start_exe && !sending && !executable) {
      storage_file_close(file);
      if (storage_file_open(file, EXE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t n = storage_file_size(file);
        if (n > 0 && n <= 16 * 1024 * 1024) {
          reset(l);
          remaining = n;
          memcpy(packet, "CEXE", 4);
          for (int i = 0; i < 4; i++)
            packet[4 + i] = (remaining >> (8 * i)) & 255;
          executable = true;
          packet_size = 8;
          sending = true;
          sent_at = furi_get_tick();
          furi_thread_flags_clear(TX);
          furi_hal_cdc_send(1, packet, 8);
          continue;
        }
        storage_file_close(file);
      }
    }
    if (sending)
      continue;
    if (executable) {
      if (!remaining) {
        storage_file_close(file);
        executable = false;
        offset = 0;
        continue;
      }
      packet_size = remaining > 64 ? 64 : remaining;
      if (storage_file_read(file, packet, packet_size) != packet_size) {
        storage_file_close(file);
        executable = false;
        continue;
      }
      remaining -= packet_size;
    } else {
      furi_mutex_acquire(l->lock, FuriWaitForever);
      if (offset >= sizeof(l->frame)) {
        offset = 0;
        l->ready = false;
      }
      if (!l->ready ||
          !(furi_hal_cdc_get_ctrl_line_state(1) & CdcCtrlLineDTR)) {
        furi_mutex_release(l->lock);
        continue;
      }
      packet_size = sizeof(l->frame) - offset;
      if (packet_size > 64)
        packet_size = 64;
      memcpy(packet, l->frame + offset, packet_size);
      furi_mutex_release(l->lock);
    }
    furi_thread_flags_clear(TX);
    sending = true;
    sent_at = furi_get_tick();
    furi_hal_cdc_send(1, packet, packet_size);
  }
  storage_file_close(file);
  storage_file_free(file);
  furi_record_close(RECORD_STORAGE);
  return 0;
}
bool usb_link_start(void) {
  if (link)
    return true;
  if (furi_hal_usb_is_locked())
    return false;
  Link *l = calloc(1, sizeof(Link));
  l->lock = furi_mutex_alloc(FuriMutexTypeNormal);
  l->rx = furi_stream_buffer_alloc(256, 1);
  l->previous = furi_hal_usb_get_config();

  l->thread = furi_thread_alloc_ex("CelesteUSB", 3072, worker, l);
  if (!furi_hal_usb_set_config(&usb_cdc_dual, NULL)) {
    furi_thread_free(l->thread);
    furi_stream_buffer_free(l->rx);
    furi_mutex_free(l->lock);
    free(l);
    return false;
  }
  link = l;
  furi_thread_start(l->thread);
  furi_hal_cdc_set_callbacks(1, &callbacks, l);
  return true;
}
void usb_link_stop(void) {
  Link *l = link;
  if (!l)
    return;
  furi_hal_cdc_set_callbacks(1, NULL, NULL);
  /* Cancel endpoint before the worker's stack packet can go out of scope. */
  furi_hal_usb_set_config(NULL, NULL);
  furi_thread_flags_set(furi_thread_get_id(l->thread), STOP);
  furi_thread_join(l->thread);
  furi_hal_usb_set_config(l->previous, NULL);
  furi_thread_free(l->thread);
  furi_stream_buffer_free(l->rx);
  furi_mutex_free(l->lock);
  free(l);
  link = NULL;
}
uint8_t usb_link_buttons(void) {
  if (!link)
    return 0;
  furi_mutex_acquire(link->lock, FuriWaitForever);
  uint8_t b = furi_get_tick() - link->last_keys < furi_ms_to_ticks(250)
                  ? link->keys
                  : 0;
  furi_mutex_release(link->lock);
  return b;
}
bool usb_link_pause(void) {
  if (!link)
    return false;
  furi_mutex_acquire(link->lock, FuriWaitForever);
  bool p = link->pause;
  link->pause = false;
  furi_mutex_release(link->lock);
  return p;
}
void usb_link_frame(const uint8_t *frame) {
  if (!link)
    return;
  furi_mutex_acquire(link->lock, FuriWaitForever);
  if (link->request && !link->ready) {
    memcpy(link->frame, "CLST", 4);
    memcpy(link->frame + 4, frame, 8192);
    link->request = false;
    link->ready = true;
    furi_thread_flags_set(furi_thread_get_id(link->thread), FRAME);
  }
  furi_mutex_release(link->lock);
}
