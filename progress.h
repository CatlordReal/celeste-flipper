#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PROGRESS_MAGIC 0x434C5331u
typedef struct {
  uint32_t magic, version, generation;
  uint32_t room, fruit, deaths, double_dash, completed;
  uint32_t total_fruit, total_deaths, playthroughs, restarts, best_room;
  uint32_t display_mode, muted, checksum;
} Progress;
void progress_default(Progress *p);
uint32_t progress_checksum(const Progress *p);
bool progress_valid(const Progress *p);
bool progress_update(Progress *p, int room, uint32_t fruit, int deaths,
                     int double_dash, bool complete);
void progress_restart(Progress *p);
