#ifndef ZK_POWER_H
#define ZK_POWER_H

#include <stddef.h>
#include <stdint.h>

/* Display power for the framebuffer kiosk.
 *
 * The clock is the caller's now_ms (monotonic milliseconds). The backlight
 * is a sysfs directory. FBIOBLANK is optional and only used for OFF when the
 * directory is missing or brightness is not writable, and only if fb_real is
 * set. There is no process-global power state. Not thread-safe: one UI thread.
 *
 * dim_after_sec 0 disables dim and off and does not open, stat, or read
 * the directory.
 * off_after_sec 0 disables OFF only. If off_after_sec is positive and not
 * strictly greater than dim_after_sec, OFF is disabled (treated as 0).
 * Off is measured from the last touch, not from the dim transition.
 * When dimming is enabled and the backlight directory is usable, init reads
 * brightness and max_brightness once. Dim and restore keep those values.
 * A dim write is min(dim level floored at 1, saved brightness, max). It never
 * raises the panel. Saved brightness below 1 (0 means already dark; a negative
 * read is treated the same) does not write on dim; the state still becomes
 * DIMMED. Wake and exit then restore max (saved below 1 becomes max). If
 * brightness could not be read, the dim write is min(dim level floored at 1,
 * max) and restore writes max. A readable original of at least 1 is never
 * dimmed below 1. Shutdown that never dimmed or blanked writes nothing.
 *
 * Wake taps:
 *   ACTIVE  any touch     PASS, idle clock resets
 *   DIMMED  STOP          PASS and wake to ACTIVE (press and release)
 *   DIMMED  any other     SWALLOW until release + 300 ms, wake to ACTIVE
 *   OFF     any touch     SWALLOW until release + 300 ms, wake to ACTIVE
 * A sample at exactly release+300 ms is delivered. Earlier samples in that
 * window are swallowed, including a second press. STOP does not arm the guard.
 * The press-edge decision is latched for the whole contact.
 *
 * Blockers (watering, lockout, fault, unreachable, modal_open) force ACTIVE.
 * While one is set the idle clock stays at zero. Paused may dim and never OFF.
 * Sysfs and blank failures are logged once and are not fatal.
 */

#define ZK_POWER_DEFAULT_DIM_AFTER_SEC 120
#define ZK_POWER_DEFAULT_OFF_AFTER_SEC 600
#define ZK_POWER_DEFAULT_DIM_LEVEL 51
#define ZK_POWER_DEFAULT_BACKLIGHT "/sys/class/backlight/rpi_backlight"
#define ZK_POWER_SYSFS_BACKLIGHT_ROOT "/sys/class/backlight"
#define ZK_POWER_WAKE_GUARD_MS 300
#define ZK_POWER_DIR_MAX 384
#define ZK_POWER_TRACE_CAP 32

/* How zk_power_resolve_backlight chose the directory. */
enum {
    ZK_POWER_BL_NONE = 0,
    ZK_POWER_BL_OVERRIDE,
    ZK_POWER_BL_RPI,
    ZK_POWER_BL_10_0045,
    ZK_POWER_BL_SCAN
};

typedef enum {
    ZK_POWER_ACTIVE = 0,
    ZK_POWER_DIMMED,
    ZK_POWER_OFF
} zk_power_state_t;

typedef enum {
    ZK_POWER_PASS = 0,
    ZK_POWER_SWALLOW
} zk_power_touch_action_t;

typedef struct {
    int watering;
    int lockout;
    int fault;
    int unreachable;
    int modal_open;
    int paused;
} zk_power_inputs_t;

/* blank: powerdown 1 = FB_BLANK_POWERDOWN, 0 = FB_BLANK_UNBLANK.
 * Return 0 on success, -1 on failure. NULL if there is no framebuffer ioctl. */
typedef int (*zk_power_blank_fn)(void *ctx, int powerdown);

typedef struct {
    int dim_after_sec;
    int off_after_sec;
    int dim_level; /* 0..255; clamped. 0 is written as 1 unless that would brighten. */
    const char *backlight_dir; /* NULL or empty: default rpi_backlight path */
    int fb_real; /* non-zero: blank fn may be used for OFF */
    zk_power_blank_fn blank;
    void *blank_ctx;
} zk_power_config_t;

typedef struct {
    char what[24];
    char value[16];
} zk_power_trace_t;

typedef struct zk_power {
    int enabled;
    int shut;
    int dim_after_sec;
    int off_after_sec;
    int dim_level;
    int64_t dim_ms;
    int64_t off_ms;
    zk_power_state_t state;
    int transitions;
    int anchored;
    int64_t last_ms;
    int finger;
    int contact_swallow;
    int64_t guard_until;
    char dir[ZK_POWER_DIR_MAX];
    int bl_ok;
    int bl_bad;
    int limits_ready;
    int have_saved;
    int saved_brightness;
    int max_brightness;
    int hw_touched;
    int fb_real;
    zk_power_blank_fn blank;
    void *blank_ctx;
    int blanked;
    int logged_br;
    int logged_bl;
    int logged_blank;
    int logged_off;
    zk_power_trace_t trace[ZK_POWER_TRACE_CAP];
    int trace_n;
} zk_power_t;

/* Pick a sysfs backlight directory. Does not read the environment.
 * root NULL/empty means ZK_POWER_SYSFS_BACKLIGHT_ROOT.
 * override is a device name under root, or a full directory path if it
 * contains '/'. Names that are "." or contain ".." are rejected.
 * A usable override (existing directory, fits in out) wins. Invalid,
 * missing, or too-long override logs one stderr line naming the value
 * and falls back to auto-detect.
 * Auto-detect: rpi_backlight if it is a directory; else 10-0045 if it is
 * a directory; else the first directory in strcmp order with a writable
 * bl_power or brightness. NONE leaves out empty; the caller may use
 * ZK_POWER_DEFAULT_BACKLIGHT so absent-device behaviour is unchanged.
 *
 * main.c passes getenv("ZAN_SYSFS_BACKLIGHT_ROOT") as root (TEST-ONLY;
 * production leaves that unset) and getenv("ZAN_BACKLIGHT") as override.
 */
int zk_power_resolve_backlight(const char *root, const char *override,
                               char *out, size_t outsz);

void zk_power_init(zk_power_t *p, const zk_power_config_t *cfg);
void zk_power_tick(zk_power_t *p, int64_t now_ms, const zk_power_inputs_t *in);
zk_power_touch_action_t zk_power_touch_event(zk_power_t *p, int64_t now_ms, int pressed,
                                            int hit_is_stop);
void zk_power_shutdown(zk_power_t *p);

int zk_power_enabled(const zk_power_t *p);
zk_power_state_t zk_power_state(const zk_power_t *p);
int zk_power_transitions(const zk_power_t *p);
int zk_power_dim_after_sec(const zk_power_t *p);
int zk_power_off_after_sec(const zk_power_t *p);
int zk_power_dim_level(const zk_power_t *p);
const char *zk_power_backlight_dir(const zk_power_t *p);
int zk_power_trace_len(const zk_power_t *p);
int zk_power_trace_at(const zk_power_t *p, int index, const char **what, const char **value);

#endif
