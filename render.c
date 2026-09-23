#include "render.h"
#include "assets.h"
#include "vendor/tilemap.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifndef HOST_TEST
#include "audio.h"
#endif
uint8_t render_frame[8192];
uint8_t render_buttons;
int render_music = -1;
static uint8_t palette[16];
static int camera_x, camera_y;
/* The USB colour frame stays untouched. The LCD has its own native world
   raster so camera movement never changes sprite geometry or game physics. */
static uint8_t lcd_world[2048];
static int background, lcd_suppress, actor;
static int player_y, player_active, last_player_active;
static int room_x = -1, room_y = -1, snap_camera;
static int follow_q8 = 32 * 256;
static unsigned draw_serial, consumed_serial;
static struct {
  int x, y, fixed;
  unsigned char glyph;
} text_glyphs[128];
static unsigned text_count;
static const uint8_t ink[16] = {0,  0,  0,  1,  1,  7, 11, 16,
                                16, 12, 16, 16, 12, 4, 16, 12};
static const uint8_t threshold[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static uint8_t nibble(const uint8_t *p, unsigned n) {
  return (p[n / 2] >> (4 * (n & 1))) & 15;
}
static void pixel(int x, int y, int c) {
  if ((unsigned)x >= 128 || (unsigned)y >= 128)
    return;
  unsigned n = y * 128 + x, s = 4 * (n & 1);
  if (!lcd_suppress) {
    int dark =
        !background && (actor ? (c != 0 && c != 15)
                              : ink[palette[c & 15]] > threshold[y & 3][x & 3]);
    uint8_t bit = 1u << (n & 7);
    if (dark)
      lcd_world[n / 8] |= bit;
    else
      lcd_world[n / 8] &= ~bit;
  }
  render_frame[n / 2] =
      (render_frame[n / 2] & ~(15u << s)) | ((unsigned)palette[c & 15] << s);
}
static void rect(int x, int y, int x1, int y1, int c) {
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x1 > 127)
    x1 = 127;
  if (y1 > 127)
    y1 = 127;
  for (int yy = y; yy <= y1; yy++)
    for (int xx = x; xx <= x1; xx++)
      pixel(xx, yy, c);
}
static void line(int x, int y, int x1, int y1, int c) {
  int dx = abs(x1 - x), sx = x < x1 ? 1 : -1, dy = -abs(y1 - y),
      sy = y < y1 ? 1 : -1, e = dx + dy;
  for (;;) {
    pixel(x, y, c);
    if (x == x1 && y == y1)
      break;
    int e2 = 2 * e;
    if (e2 >= dy) {
      e += dy;
      x += sx;
    }
    if (e2 <= dx) {
      e += dx;
      y += sy;
    }
  }
}
static void sprite(const uint8_t *data, int tile, int x, int y, int fx, int fy,
                   int override) {
  if (tile < 0 || tile > 255)
    return;
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
      int c = nibble(data, ((tile / 16) * 8 + (fy ? 7 - j : j)) * 128 +
                               (tile % 16) * 8 + (fx ? 7 - i : i));
      if (c)
        pixel(x + i, y + j, override >= 0 ? override : c);
    }
}
void render_init(void) {
  for (int i = 0; i < 16; i++)
    palette[i] = i;
  camera_x = camera_y = 0;
  render_buttons = 0;
  player_active = last_player_active = text_count = 0;
  room_x = room_y = -1;
  follow_q8 = 32 * 256;
  draw_serial = consumed_serial = 0;
  background = lcd_suppress = actor = snap_camera = 0;
  memset(lcd_world, 0, sizeof lcd_world);
  memset(render_frame, 0, sizeof render_frame);
}
int render_callback(CELESTE_P8_CALLBACK_TYPE call, ...) {
  va_list ap;
  va_start(ap, call);
  int result = 0;
#define I() va_arg(ap, int)
  switch (call) {
  case CELESTE_P8_BTN: {
    int b = I();
    result = b >= 0 && b < 6 && ((render_buttons >> b) & 1);
    break;
  }
  case CELESTE_P8_MGET: {
    int x = I(), y = I();
    result =
        (unsigned)x < 128 && (unsigned)y < 64 ? tilemap_data[y * 128 + x] : 0;
    break;
  }
  case CELESTE_P8_FGET: {
    int t = I(), f = I();
    result = (unsigned)t < sizeof(tile_flags) && (unsigned)f < 8 &&
             ((tile_flags[t] >> f) & 1);
    break;
  }
  case CELESTE_P8_CAMERA:
    camera_x = I();
    camera_y = I();
    break;
  case CELESTE_P8_PAL: {
    int a = I(), b = I();
    if ((unsigned)a < 16 && (unsigned)b < 16)
      palette[a] = b;
    break;
  }
  case CELESTE_P8_PAL_RESET:
    for (int i = 0; i < 16; i++)
      palette[i] = i;
    break;
  case CELESTE_P8_RECTFILL: {
    int x = I(), y = I(), x1 = I(), y1 = I(), c = I();
    if (x == 0 && y == 0 && x1 == 128 && y1 == 128) {
      background = 1;
      player_active = 0;
      text_count = 0;
      draw_serial++;
    }
    /* Text panels are redrawn in screen space, including the memorial's
       character-by-character reveal and summit statistics. */
    lcd_suppress =
        (x == 24 && y == 58 && x1 == 104 && y1 == 70) ||
        (x == 32 && y == 2 && x1 == 96 && y1 == 31) ||
        (((x == 4 && y == 4) || (x == 49 && y == 16)) && x1 == x + 32 &&
         y1 == y + 6) ||
        (c == 7 && y >= 94 && y <= 108 && x1 == x + 9 && y1 == y + 8);
    rect(x - camera_x, y - camera_y, x1 - camera_x, y1 - camera_y, c);
    lcd_suppress = 0;
    break;
  }
  case CELESTE_P8_LINE: {
    int x = I(), y = I(), x1 = I(), y1 = I(), c = I();
    line(x - camera_x, y - camera_y, x1 - camera_x, y1 - camera_y, c);
    break;
  }
  case CELESTE_P8_CIRCFILL: {
    int x = I() - camera_x, y = I() - camera_y, r = I(), c = I();
    for (int j = -r; j <= r; j++)
      for (int i = -r; i <= r; i++)
        if (i * i + j * j <= r * r + r / 2)
          pixel(x + i, y + j, c);
    break;
  }
  case CELESTE_P8_SPR: {
    int t = I(), x = I(), y = I(), w = I(), h = I(), fx = I(), fy = I();
    (void)w;
    (void)h;
    actor = t >= 1 && t <= 7;
    if (actor) {
      player_active = 1;
      player_y = y;
    }
    sprite(gfx_pixels, t, x - camera_x, y - camera_y, fx, fy, -1);
    actor = 0;
    break;
  }
  case CELESTE_P8_PRINT: {
    const char *s = va_arg(ap, const char *);
    int x = I(), y = I(), c = I();
    int tx = x, ty = y, fixed = 0;
    size_t length = strlen(s);
    if (y == 62 &&
        (strstr(s, " m") || !strcmp(s, "old site") || !strcmp(s, "summit"))) {
      tx = (128 - (int)length * 4 + 1) / 2;
      ty = 12;
      fixed = 1;
    } else if ((x == 5 && y == 5) || y == 9 || y == 17 || y == 24) {
      fixed = 1;
    } else if (c == 0 && y >= 96 && y <= 110) {
      ty = y - 66;
      fixed = 1; /* Three memorial lines remain together. */
    } else if (c == 5 && (y == 80 || y == 96 || y == 102)) {
      ty = y - 48;
      fixed = 1;
    }
    /* Clamp each whole string before storing glyphs, not each letter. */
    if (tx < 1)
      tx = 1;
    if (tx + (int)length * 4 > 127)
      tx = 127 - (int)length * 4;
    for (unsigned k = 0; s[k] && text_count < 128; k++) {
      text_glyphs[text_count].x = tx + 4 * k;
      text_glyphs[text_count].y = ty;
      text_glyphs[text_count].fixed = fixed;
      text_glyphs[text_count++].glyph = (unsigned char)s[k] & 127;
    }
    lcd_suppress = 1;
    for (; *s; s++, x += 4)
      sprite(font_pixels, *s & 127, x - camera_x, y - camera_y, 0, 0, c);
    lcd_suppress = 0;
    break;
  }
  case CELESTE_P8_MAP: {
    int mx = I(), my = I(), x = I() - camera_x, y = I() - camera_y, w = I(),
        h = I(), mask = I();
    if (mask == 4 && (mx != room_x || my != room_y)) {
      room_x = mx;
      room_y = my;
      snap_camera = 1;
    }
    background = mask == 4;
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        int a = mx + i, b = my + j;
        if ((unsigned)a >= 128 || (unsigned)b >= 64)
          continue;
        int t = tilemap_data[b * 128 + a],
            f = t < (int)sizeof(tile_flags) ? tile_flags[t] : 0;
        if (!mask || (mask == 4 ? f == 4 : ((f >> (mask - 1)) & 1)))
          sprite(gfx_pixels, t, x + i * 8, y + j * 8, 0, 0, -1);
      }
    background = 0;
    break;
  }
  case CELESTE_P8_MUSIC: {
    render_music = I();
#ifndef HOST_TEST
    audio_music(render_music);
#endif
    break;
  }
  case CELESTE_P8_SFX: {
    int s = I();
#ifndef HOST_TEST
    audio_sfx(s);
#else
    (void)s;
#endif
    break;
  }
  }
  va_end(ap);
  return result;
#undef I
}
static int clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}
static void lcd_pixel(uint8_t output[1024], int x, int y, int dark) {
  if ((unsigned)x >= 128 || (unsigned)y >= 64)
    return;
  uint8_t bit = 1u << (x & 7);
  if (dark)
    output[y * 16 + x / 8] |= bit;
  else
    output[y * 16 + x / 8] &= ~bit;
}
void render_mono(uint8_t output[1024], int mode) {
  /* Advance once per completed game draw. Hit-stop and repeated USB/UI reads
     retain the exact image. Follow the unshaken player, with a two-pixel
     dead zone and subpixel smoothing; clamp at room edges. */
  if (consumed_serial != draw_serial) {
    consumed_serial = draw_serial;
    if (player_active) {
      int target = clamp(player_y + 4 - 32, 0, 64) * 256;
      if (snap_camera || !last_player_active)
        follow_q8 = target;
      else {
        int delta = target - follow_q8;
        if (delta > 512)
          follow_q8 += (delta - 512) * 3 / 4;
        if (delta < -512)
          follow_q8 += (delta + 512) * 3 / 4;
        /* Even a dash keeps the centre within four pixels of the dead zone. */
        follow_q8 = clamp(follow_q8, target - 1024, target + 1024);
      }
      follow_q8 = clamp(follow_q8, 0, 64 * 256);
      snap_camera = 0;
    }
    last_player_active = player_active;
  }
  int top = (follow_q8 + 128) / 256;
  memset(output, 0, 1024);
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 128; x++) {
      if (mode == 1 && (x < 32 || x >= 96))
        continue;
      int sx = mode == 1 ? (x - 32) * 2 : x;
      int sy = mode == 1 ? y * 2 : y + top;
      int dark = 0;
      for (int j = 0; j < (mode == 1 ? 2 : 1); j++)
        for (int i = 0; i < (mode == 1 ? 2 : 1); i++) {
          unsigned n = (sy + j) * 128 + sx + i;
          dark |= (lcd_world[n / 8] >> (n & 7)) & 1;
        }
      lcd_pixel(output, x, y, dark);
    }
  /* All text is a screen-space overlay. Clear every panel first so adjacent
     letters (including the memorial reveal) cannot erase previous glyphs. */
  for (unsigned k = 0; k < text_count; k++) {
    int x = text_glyphs[k].x;
    int y = text_glyphs[k].fixed ? text_glyphs[k].y
            : mode == 1          ? text_glyphs[k].y / 2
                                 : text_glyphs[k].y - top;
    y = clamp(y, 1, 58);
    for (int j = -1; j <= 5; j++)
      for (int i = -1; i <= 3; i++)
        lcd_pixel(output, x + i, y + j, 0);
  }
  for (unsigned k = 0; k < text_count; k++) {
    int x = text_glyphs[k].x, t = text_glyphs[k].glyph;
    int y = text_glyphs[k].fixed ? text_glyphs[k].y
            : mode == 1          ? text_glyphs[k].y / 2
                                 : text_glyphs[k].y - top;
    y = clamp(y, 1, 58);
    for (int j = 0; j < 5; j++)
      for (int i = 0; i < 3; i++)
        if (nibble(font_pixels, (t / 16 * 8 + j) * 128 + t % 16 * 8 + i))
          lcd_pixel(output, x + i, y + j, 1);
  }
}
