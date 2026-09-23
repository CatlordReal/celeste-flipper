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
static uint8_t scenery[2048];
static int background;
static uint8_t nibble(const uint8_t *p, unsigned n) {
  return (p[n / 2] >> (4 * (n & 1))) & 15;
}
static void pixel(int x, int y, int c) {
  if ((unsigned)x >= 128 || (unsigned)y >= 128)
    return;
  unsigned n = y * 128 + x, s = 4 * (n & 1);
  if (background)
    scenery[n / 8] |= 1u << (n & 7);
  else
    scenery[n / 8] &= ~(1u << (n & 7));
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
    if (x == 0 && y == 0 && x1 == 128 && y1 == 128)
      background = 1;
    rect(x - camera_x, y - camera_y, x1 - camera_x, y1 - camera_y, c);
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
    sprite(gfx_pixels, t, x - camera_x, y - camera_y, fx, fy, -1);
    break;
  }
  case CELESTE_P8_PRINT: {
    const char *s = va_arg(ap, const char *);
    int x = I() - camera_x, y = I() - camera_y, c = I();
    for (; *s; s++, x += 4)
      sprite(font_pixels, *s & 127, x, y, 0, 0, c);
    break;
  }
  case CELESTE_P8_MAP: {
    int mx = I(), my = I(), x = I() - camera_x, y = I() - camera_y, w = I(),
        h = I(), mask = I();
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
void render_mono(uint8_t output[1024], int mode) {
  /* Preserve bright hazards/actors from either source row. Stable spatial
     stippling separates muted mountain/stone without LCD temporal flicker. */
  static const uint8_t ink[16] = {0,  0,  0,  1,  1,  7, 11, 16,
                                  16, 12, 16, 16, 12, 4, 16, 12};
  static const uint8_t threshold[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  memset(output, 0, 1024);
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 128; x++) {
      int sx = x, ox = x;
      if (mode == 1) {
        if (x < 32 || x >= 96)
          continue;
        sx = (x - 32) * 2;
      }
      int a = ink[nibble(render_frame, y * 256 + sx)],
          b = ink[nibble(render_frame, y * 256 + 128 + sx)];
      if (scenery[(y * 256 + sx) / 8] & (1u << (sx & 7)))
        a = 0;
      if (scenery[(y * 256 + 128 + sx) / 8] & (1u << (sx & 7)))
        b = 0;
      int v = a > b ? a : b;
      if (mode == 1) {
        for (int j = 0; j < 2; j++) {
          int c = ink[nibble(render_frame, y * 256 + j * 128 + sx + 1)];
          if (!(scenery[(y * 256 + j * 128 + sx + 1) / 8] &
                (1u << ((sx + 1) & 7))) &&
              c > v)
            v = c;
        }
      }
      if (v > threshold[y & 3][x & 3])
        output[y * 16 + ox / 8] |= 1u << (ox & 7);
    }
}
