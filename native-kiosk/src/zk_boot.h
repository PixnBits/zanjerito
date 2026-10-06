#ifndef ZK_BOOT_H
#define ZK_BOOT_H

#include <stdint.h>

/* Two one-shot full-screen redraws after the first rendered frame, so a late
 * Plymouth clear of fb0 cannot leave a blank static Home screen.
 * Delays are milliseconds after first_frame_ms. At most two fires. */

#define ZK_BOOT_REPAINT_MS_1 2000
#define ZK_BOOT_REPAINT_MS_2 5000

typedef struct {
    unsigned fired; /* 0, 1, or 2 */
} zk_boot_repaint_t;

/* 1 if a forced redraw should happen now. Updates *state.
 * Elapsed is (uint64_t)now_ms - (uint64_t)first_frame_ms so a clock wrap
 * still yields the true delta. NULL state => 0. Does not look at power:
 * the caller must skip the paint when zk_power is DIMMED or OFF. */
int zk_boot_repaint_due(int64_t now_ms, int64_t first_frame_ms, zk_boot_repaint_t *state);

#endif
