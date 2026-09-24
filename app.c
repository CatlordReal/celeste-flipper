#include "audio.h"
#include "progress.h"
#include "render.h"
#include "usb_link.h"
#include "windows_launch.h"
#include "xp.h"
#include <dolphin/dolphin.h>
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <stdlib.h>
#include <storage/storage.h>
#include <string.h>
#define SAVE_A EXT_PATH("apps_data/celeste_classic/save_a.bin")
#define SAVE_B EXT_PATH("apps_data/celeste_classic/save_b.bin")
typedef struct {
  FuriMutex *mutex;
  FuriMessageQueue *events;
  Storage *storage;
  ViewPort *viewport;
  Progress progress;
  uint8_t mono[1024];
  uint8_t held, pressed;
  bool menu, stats, confirm_restart, save_failed, dirty, running, usb;
  uint32_t pending_berries;
  bool pending_win;
  bool open_menu, launch_pending, host_error;
  int host_selected;
  int selected;
} App;
static bool read_slot(Storage *storage, const char *path, Progress *p) {
  File *file = storage_file_alloc(storage);
  bool ok = false;
  if (storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
    ok = storage_file_size(file) == sizeof(*p) &&
         storage_file_read(file, p, sizeof(*p)) == sizeof(*p) &&
         progress_valid(p);
  }
  storage_file_close(file);
  storage_file_free(file);
  return ok;
}
static bool save(App *app) {
  Progress next = app->progress;
  next.generation++;
  next.checksum = progress_checksum(&next);
  storage_common_mkdir(app->storage, EXT_PATH("apps_data/celeste_classic"));
  const char *path = next.generation & 1 ? SAVE_A : SAVE_B;
  File *file = storage_file_alloc(app->storage);
  bool ok = false;
  if (storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
    ok = storage_file_write(file, &next, sizeof(next)) == sizeof(next) &&
         storage_file_sync(file);
  }
  storage_file_close(file);
  storage_file_free(file);
  Progress verify;
  if (ok)
    ok = read_slot(app->storage, path, &verify) &&
         memcmp(&next, &verify, sizeof(next)) == 0;
  if (ok) {
    app->progress = next;
    app->dirty = false;
    xp_award(app->pending_berries, app->pending_win);
    app->pending_berries = 0;
    app->pending_win = false;
  }
  app->save_failed = !ok;
  return ok;
}
static void load(App *app) {
  Progress a, b;
  bool va = read_slot(app->storage, SAVE_A, &a),
       vb = read_slot(app->storage, SAVE_B, &b);
  progress_default(&app->progress);
  if (va && (!vb || (int32_t)(a.generation - b.generation) > 0))
    app->progress = a;
  else if (vb)
    app->progress = b;
}
static void draw(Canvas *canvas, void *ctx) {
  App *a = ctx;
  furi_mutex_acquire(a->mutex, FuriWaitForever);
  canvas_clear(canvas);
  canvas_set_color(canvas, ColorBlack);
  if (!a->menu) {
    canvas_draw_xbm(canvas, 0, 0, 128, 64, a->mono);
    if (a->save_failed) {
      canvas_set_color(canvas, ColorWhite);
      canvas_draw_box(canvas, 0, 0, 58, 9);
      canvas_set_color(canvas, ColorBlack);
      canvas_set_font(canvas, FontSecondary);
      canvas_draw_str(canvas, 1, 8, "SAVE ERROR");
    }
  } else {
    canvas_set_font(canvas, FontSecondary);
    char text[40];
    if (a->open_menu) {
      const char *host[] = {"Windows: open", "Mac: manual USB",
                            "Linux: manual USB", "Disconnect USB", "Back"};
      for (int i = 0; i < 5; i++) {
        if (i == a->host_selected)
          canvas_draw_str(canvas, 1, 10 + i * 10, ">");
        canvas_draw_str(canvas, 10, 10 + i * 10, host[i]);
      }
      canvas_draw_str(canvas, 1, 62,
                      a->launch_pending ? "Opening Windows..."
                      : a->host_error   ? "USB/file unavailable"
                                        : "Windows: US keyboard");
    } else if (a->confirm_restart) {
      canvas_draw_str(canvas, 1, 12, "Restart from first room?");
      canvas_draw_str(canvas, 1, 28, "Lifetime stats stay saved.");
      canvas_draw_str(canvas, 1, 46, "OK: restart");
      canvas_draw_str(canvas, 1, 60, "Back: cancel");
    } else if (a->stats) {
      snprintf(text, sizeof text, "Summits: %lu",
               (unsigned long)a->progress.playthroughs);
      canvas_draw_str(canvas, 1, 10, text);
      snprintf(text, sizeof text, "Berries: %lu",
               (unsigned long)a->progress.total_fruit);
      canvas_draw_str(canvas, 1, 21, text);
      snprintf(text, sizeof text, "Deaths: %lu",
               (unsigned long)a->progress.total_deaths);
      canvas_draw_str(canvas, 1, 32, text);
      snprintf(text, sizeof text, "Restarts: %lu",
               (unsigned long)a->progress.restarts);
      canvas_draw_str(canvas, 1, 43, text);
      snprintf(text, sizeof text, "Best: %lum  Back: menu",
               (unsigned long)(a->progress.best_room + 1) * 100);
      canvas_draw_str(canvas, 1, 59, text);
    } else {
      const char *items[] = {
          "Open on computer",
          "Resume",
          "Restart run",
          "Lifetime stats",
          a->progress.display_mode ? "View: overview" : "View: follow",
          a->progress.muted ? "Sound: off" : "Sound: on",
          a->save_failed ? "Retry save + exit" : "Save + exit"};
      for (int i = 0; i < 6; i++) {
        int row = a->selected >= 5 ? i + 1 : i;
        if (row == a->selected)
          canvas_draw_str(canvas, 1, 10 + i * 10, ">");
        canvas_draw_str(canvas, 10, 10 + i * 10, items[row]);
      }
    }
  }
  furi_mutex_release(a->mutex);
}
static void input(InputEvent *event, void *ctx) {
  App *a = ctx;
  furi_message_queue_put(a->events, event, 0);
}
static int keybit(InputKey k) {
  switch (k) {
  case InputKeyLeft:
    return 0;
  case InputKeyRight:
    return 1;
  case InputKeyUp:
    return 2;
  case InputKeyDown:
    return 3;
  case InputKeyOk:
    return 4;
  case InputKeyBack:
    return 5;
  default:
    return -1;
  }
}
static void event(App *a, InputEvent e) {
  int bit = keybit(e.key);
  if (bit < 0)
    return;
  if (e.type == InputTypeRelease)
    a->held &= ~(1u << bit);
  if (e.type == InputTypePress) {
    a->held |= 1u << bit;
    if (!a->menu)
      a->pressed |= 1u << bit;
  }
  if (!a->menu) {
    if (e.key == InputKeyBack && e.type == InputTypeLong) {
      a->menu = true;
      a->held = a->pressed = 0;
      audio_music(-1);
    }
    return;
  }
  if (e.type != InputTypeShort && e.type != InputTypeRepeat)
    return;
  if (a->open_menu) {
    if (e.key == InputKeyBack && e.type == InputTypeShort) {
      a->open_menu = false;
      return;
    }
    if (e.key == InputKeyUp)
      a->host_selected = (a->host_selected + 4) % 5;
    if (e.key == InputKeyDown)
      a->host_selected = (a->host_selected + 1) % 5;
    if (e.key == InputKeyOk && e.type == InputTypeShort) {
      if (a->host_selected == 0) {
        a->launch_pending = true;
        a->host_error = false;
      } else if (a->host_selected == 1 || a->host_selected == 2) {
        a->usb = usb_link_start();
        a->host_error = !a->usb;
        if (a->usb) {
          a->open_menu = a->menu = false;
          audio_music(render_music);
        }
      } else if (a->host_selected == 3) {
        usb_link_stop();
        a->usb = false;
      } else
        a->open_menu = false;
    }
    return;
  }
  if (e.key == InputKeyBack && e.type == InputTypeShort) {
    if (a->stats || a->confirm_restart) {
      a->stats = a->confirm_restart = false;
    } else {
      a->menu = false;
      a->held = a->pressed = 0;
      if (!a->progress.muted)
        audio_music(render_music);
    }
    return;
  }
  if (a->stats)
    return;
  if (a->confirm_restart) {
    if (e.key == InputKeyOk && e.type == InputTypeShort) {
      progress_restart(&a->progress);
      a->dirty = true;
      save(a);
      Celeste_Flipper_restart();
      a->confirm_restart = false;
      a->menu = false;
      a->held = a->pressed = 0;
    }
    return;
  }
  if (e.key == InputKeyUp)
    a->selected = (a->selected + 6) % 7;
  if (e.key == InputKeyDown)
    a->selected = (a->selected + 1) % 7;
  if (e.key != InputKeyOk || e.type != InputTypeShort)
    return;
  switch (a->selected) {
  case 0:
    a->open_menu = true;
    a->host_selected = 0;
    break;
  case 1:
    a->menu = false;
    a->held = a->pressed = 0;
    if (!a->progress.muted)
      audio_music(render_music);
    break;
  case 2:
    a->confirm_restart = true;
    break;
  case 3:
    a->stats = true;
    break;
  case 4:
    a->progress.display_mode ^= 1;
    a->dirty = true;
    save(a);
    break;
  case 5:
    a->progress.muted ^= 1;
    audio_set_enabled(!a->progress.muted);
    a->dirty = true;
    save(a);
    break;
  case 6:
    if (save(a))
      a->running = false;
    break;
  }
}
int32_t celeste_app(void *context) {
  UNUSED(context);
  App *a = calloc(1, sizeof(App));
  a->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
  a->events = furi_message_queue_alloc(32, sizeof(InputEvent));
  a->storage = furi_record_open(RECORD_STORAGE);
  load(a);
  render_init();
  audio_init();
  audio_set_enabled(!a->progress.muted);
  Celeste_P8_set_call_func(render_callback);
  Celeste_P8_set_rndseed(furi_get_tick());
  Celeste_P8_init();
  Celeste_Flipper_resume(a->progress.room, a->progress.fruit,
                         a->progress.deaths, a->progress.double_dash);
  Celeste_Flipper_set_completed(a->progress.completed);
  Gui *gui = furi_record_open(RECORD_GUI);
  NotificationApp *notification = furi_record_open(RECORD_NOTIFICATION);
  notification_message(notification, &sequence_display_backlight_enforce_on);
  a->viewport = view_port_alloc();
  view_port_draw_callback_set(a->viewport, draw, a);
  view_port_input_callback_set(a->viewport, input, a);
  gui_add_view_port(gui, a->viewport, GuiLayerFullscreen);
  a->running = true;
  uint32_t next = furi_get_tick();
  uint32_t remainder = 0;
  while (a->running) {
    if (a->launch_pending) {
      view_port_update(a->viewport);
      bool available =
          storage_common_stat(
              a->storage,
              EXT_PATH("apps_data/celeste_classic/Celeste-Windows.exe"),
              NULL) == FSE_OK;
      if (a->usb) {
        usb_link_stop();
        a->usb = false;
      }
      bool launched = available && windows_launch();
      furi_mutex_acquire(a->mutex, FuriWaitForever);
      a->usb = launched && usb_link_start();
      a->host_error = !a->usb;
      a->launch_pending = false;
      if (a->usb) {
        a->open_menu = a->menu = false;
        a->held = a->pressed = 0;
        audio_music(render_music);
      }
      furi_mutex_release(a->mutex);
    }
    InputEvent e;
    uint32_t now = furi_get_tick();
    int32_t wait = (int32_t)(next - now);
    if (wait < 0)
      wait = 0;
    if (furi_message_queue_get(a->events, &e, wait) == FuriStatusOk) {
      furi_mutex_acquire(a->mutex, FuriWaitForever);
      event(a, e);
      furi_mutex_release(a->mutex);
      continue;
    }
    furi_mutex_acquire(a->mutex, FuriWaitForever);
    if (usb_link_pause()) {
      a->menu = !a->menu;
      a->held = a->pressed = 0;
      audio_music(a->menu ? -1 : render_music);
    }
    if (!a->menu) {
      render_buttons = a->held | a->pressed | usb_link_buttons();
      a->pressed = 0;
      Celeste_P8_update();
      Celeste_P8_draw();
      render_mono(a->mono, a->progress.display_mode);
      uint32_t previous_fruit = a->progress.total_fruit;
      bool previous_complete = a->progress.completed;
      bool changed = progress_update(
          &a->progress, Celeste_Flipper_room(), Celeste_Flipper_fruit(),
          Celeste_Flipper_deaths(), Celeste_Flipper_double_dash(),
          Celeste_Flipper_completed());
      if (changed) {
        a->pending_berries += a->progress.total_fruit - previous_fruit;
        if (!previous_complete && a->progress.completed)
          a->pending_win = true;
        a->dirty = true;
        save(a);
      }
      usb_link_frame(render_frame);
    }
    audio_tick();
    furi_mutex_release(a->mutex);
    view_port_update(a->viewport);
    remainder += furi_kernel_get_tick_frequency();
    next += remainder / 30;
    remainder %= 30;
    if ((int32_t)(furi_get_tick() - next) > (int32_t)furi_ms_to_ticks(100))
      next = furi_get_tick();
  }
  usb_link_stop();
  audio_deinit();
  view_port_enabled_set(a->viewport, false);
  gui_remove_view_port(gui, a->viewport);
  view_port_free(a->viewport);
  notification_message(notification, &sequence_display_backlight_enforce_auto);
  furi_record_close(RECORD_NOTIFICATION);
  furi_record_close(RECORD_GUI);
  furi_record_close(RECORD_STORAGE);
  furi_message_queue_free(a->events);
  furi_mutex_free(a->mutex);
  free(a);
  return 0;
}
