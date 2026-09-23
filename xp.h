#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Award Momentum Dolphin XP for newly persisted achievements.
 *
 * berries is clamped to 30 and summit adds 10. Call only for first-time
 * achievements; this adapter does not own the game's persistent award ledger.
 * Returns the number of XP points applied before any pending level-up gate.
 */
uint32_t xp_award(uint32_t berries, bool summit);

#ifdef __cplusplus
}
#endif
