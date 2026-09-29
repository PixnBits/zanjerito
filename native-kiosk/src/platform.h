#ifndef ZK_PLATFORM_H
#define ZK_PLATFORM_H

#include <stdint.h>

#include "lvgl.h"

typedef struct {
    const char *fb_path; /* NULL: do not open a framebuffer */
    const char *touch_path; /* NULL: see autodetect_touch */
    int touch_swap;
    int touch_flip_x;
    int touch_flip_y;
    int autodetect_touch;
    int virtual_pointer; /* scripted taps */
} zk_platform_opts_t;

void zk_platform_mark_start(void);
double zk_platform_mono(void);
double zk_platform_since_main(void);

int zk_platform_init(const zk_platform_opts_t *opts);
lv_display_t *zk_platform_display(void);

/* lv_timer_handler plus render-time accounting. */
uint32_t zk_platform_handle_timers(void);
/* Finger down or an LVGL animation is running. */
int zk_platform_busy(void);
/* Sleep until timeout, touch, or zk_platform_wake. Drains the touch fd. */
void zk_platform_wait(uint32_t timeout_ms);
void zk_platform_wake(void);

void zk_platform_pointer(int x, int y, int pressed);
void zk_platform_note_injected_touch(void);

/* Force a refresh, then write the last flushed frame as PNG. */
int zk_snapshot_png(const char *file);

void zk_platform_print_stats(int enabled);

#endif
