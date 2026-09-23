#include "progress.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
static uint32_t add(uint32_t a, uint32_t b) {
  return UINT32_MAX - a < b ? UINT32_MAX : a + b;
}
uint32_t progress_checksum(const Progress *p) {
  const unsigned char *b = (const unsigned char *)p;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < offsetof(Progress, checksum); i++)
    h = (h ^ b[i]) * 16777619u;
  return h;
}
void progress_default(Progress *p) {
  memset(p, 0, sizeof(*p));
  p->magic = PROGRESS_MAGIC;
  p->version = 1;
  p->double_dash = 1;
  p->checksum = progress_checksum(p);
}
bool progress_valid(const Progress *p) {
  return p->magic == PROGRESS_MAGIC && p->version == 1 && p->room <= 30 &&
         p->double_dash >= 1 && p->double_dash <= 2 && p->deaths <= INT_MAX &&
         p->completed <= 1 && p->display_mode <= 1 && p->muted <= 1 &&
         !(p->fruit & 0xc0000000u) && p->checksum == progress_checksum(p);
}
bool progress_update(Progress *p, int room, uint32_t fruit, int deaths,
                     int double_dash, bool complete) {
  if (room < 0 || room > 30 || deaths < 0 || double_dash < 1 ||
      double_dash > 2 || (fruit & 0xc0000000u))
    return false;
  bool changed = p->room != (uint32_t)room || p->fruit != fruit ||
                 p->deaths != (uint32_t)deaths ||
                 p->double_dash != (uint32_t)double_dash ||
                 (!p->completed && complete);
  uint32_t fresh = fruit & ~p->fruit;
  while (fresh) {
    p->total_fruit = add(p->total_fruit, 1);
    fresh &= fresh - 1;
  }
  if ((uint32_t)deaths > p->deaths)
    p->total_deaths = add(p->total_deaths, (uint32_t)deaths - p->deaths);
  if (complete && !p->completed) {
    p->playthroughs = add(p->playthroughs, 1);
    p->completed = 1;
  }
  p->room = room;
  p->fruit = fruit;
  p->deaths = deaths;
  p->double_dash = double_dash;
  if (p->room > p->best_room)
    p->best_room = p->room;
  return changed;
}
void progress_restart(Progress *p) {
  p->room = p->fruit = p->deaths = p->completed = 0;
  p->double_dash = 1;
  p->restarts = add(p->restarts, 1);
}
