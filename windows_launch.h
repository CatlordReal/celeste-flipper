#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Type the bounded Windows bootstrap through USB HID.
 *
 * Requires Windows' active keyboard layout to be US English. The previous USB
 * interface is restored on every path; caller can then start the game link.
 * Returns false without changing USB when another app has locked its interface.
 */
bool windows_launch(void);

#ifdef __cplusplus
}
#endif
