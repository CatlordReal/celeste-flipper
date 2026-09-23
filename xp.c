#include "xp.h"

#include <dolphin/dolphin.h>
#include <furi.h>

#define XP_MAX_BERRIES 30U
#define XP_SUMMIT_BONUS 10U

uint32_t xp_award(uint32_t berries, bool summit) {
    const uint32_t berry_xp = MIN(berries, XP_MAX_BERRIES);
    const uint32_t requested = berry_xp + (summit ? XP_SUMMIT_BONUS : 0U);
    if(requested == 0U) return 0U;

    Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
    DolphinStats stats = dolphin_stats(dolphin);
    uint32_t awarded = 0U;

    while(awarded < requested && !stats.level_up_is_pending && stats.icounter < UINT32_MAX) {
        /*
         * Momentum mntm-012 exposes no semantic game-achievement XP deed.
         * Its exported test-right deed is the only exact +1 public API. It also
         * resets Dolphin mood to BUTTHURT_MIN; keep this compatibility boundary
         * isolated here so a future supported deed can replace it.
         */
        dolphin_deed(DolphinDeedTestRight);
        stats = dolphin_stats(dolphin);
        awarded++;
    }

    if(awarded > 0U) dolphin_flush(dolphin);
    furi_record_close(RECORD_DOLPHIN);
    return awarded;
}
