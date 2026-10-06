#ifndef ZK_PLATFORM_H
#define ZK_PLATFORM_H

#include <stdint.h>

#include "lvgl.h"
#include "zk_power.h"

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

/* Unsupported framebuffer on the long-running --fb path. sysexits EX_CONFIG.
 * --fbshot does not use this status. Other init failures stay exit status 1. */
#define ZK_EXIT_FB_UNSUPPORTED 78

/* 0 on success. ZK_EXIT_FB_UNSUPPORTED when the format cannot be drawn.
 * -1 on any other failure. */
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

/* Last flushed memory-display frame, XRGB8888 (B,G,R,X). NULL if none yet. */
const uint8_t *zk_platform_frame(int *w, int *h, int *stride);

void zk_platform_print_stats(int enabled);

int64_t zk_platform_mono_ms(void);
/* NULL clears the gate. The pointer must outlive the indev reads. */
void zk_platform_bind_power(zk_power_t *power);
int zk_platform_fb_is_real(void);
/* ctx is unused. powerdown 1 blanks, 0 unblanks. 0 on success, -1 on failure. */
int zk_platform_fbio_blank(void *ctx, int powerdown);
void zk_platform_set_power_transitions(int n);

#endif
