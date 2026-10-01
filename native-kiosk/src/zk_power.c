#include "zk_power.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Seconds and max_brightness are capped so dim_ms = sec * 1000 fits in int64. */
#define ZK_POWER_SEC_CAP 1000000000
#define ZK_POWER_MAX_BR_CAP 100000

static void trace_add(zk_power_t *p, const char *what, const char *value)
{
    zk_power_trace_t *t;

    if (!p || p->trace_n < 0 || p->trace_n >= ZK_POWER_TRACE_CAP) {
        return;
    }
    t = &p->trace[p->trace_n++];
    snprintf(t->what, sizeof t->what, "%s", what ? what : "");
    snprintf(t->value, sizeof t->value, "%s", value ? value : "");
}

static int join_path(char *out, size_t n, const char *dir, const char *name)
{
    int wrote;

    if (!out || n == 0 || !dir || !dir[0] || !name) {
        return -1;
    }
    wrote = snprintf(out, n, "%s/%s", dir, name);
    if (wrote < 0 || (size_t)wrote >= n) {
        return -1;
    }
    return 0;
}

static int read_int_file(const char *path, int *out)
{
    char buf[64];
    int fd;
    ssize_t n;
    char *end;
    long v;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';
    errno = 0;
    v = strtol(buf, &end, 10);
    if (errno || end == buf || v > 1000000000L || v < -1000000000L) {
        return -1;
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        end++;
    }
    if (*end != '\0') {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int write_int_file(const char *path, int value)
{
    char buf[32];
    int fd;
    int n;
    int err;
    ssize_t w;

    n = snprintf(buf, sizeof buf, "%d\n", value);
    if (n <= 0 || n >= (int)sizeof buf) {
        errno = EINVAL;
        return -1;
    }
    /* O_CREAT lets a missing node in a test tree be written. Real sysfs rejects it. */
    fd = open(path, O_WRONLY | O_CLOEXEC | O_TRUNC | O_CREAT, 0644);
    if (fd < 0) {
        return -1;
    }
    w = write(fd, buf, (size_t)n);
    err = errno;
    close(fd);
    if (w != (ssize_t)n) {
        errno = err ? err : EIO;
        return -1;
    }
    return 0;
}

static void log_once(int *flag, const char *msg)
{
    if (!flag || *flag) {
        return;
    }
    *flag = 1;
    fprintf(stderr, "%s\n", msg);
}

static void copy_dir(zk_power_t *p, const char *dir)
{
    const char *src = (dir && dir[0]) ? dir : ZK_POWER_DEFAULT_BACKLIGHT;
    size_t n = strlen(src);

    p->dir[0] = '\0';
    if (n == 0 || n >= sizeof p->dir) {
        return;
    }
    memcpy(p->dir, src, n + 1);
}

static void probe_backlight(zk_power_t *p)
{
    char path[ZK_POWER_DIR_MAX + 32];
    struct stat st;

    p->bl_ok = 0;
    p->bl_bad = 0;
    if (!p->dir[0]) {
        p->bl_bad = 1;
        return;
    }
    if (stat(p->dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return;
    }
    if (join_path(path, sizeof path, p->dir, "brightness") != 0) {
        p->bl_bad = 1;
        return;
    }
    if (stat(path, &st) != 0) {
        /* Missing file: original is unknown. A later write may create it. */
        if (errno == ENOENT) {
            p->bl_ok = 1;
            return;
        }
        p->bl_bad = 1;
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        p->bl_bad = 1;
        return;
    }
    p->bl_ok = 1;
}

/* Called from init only, and only when the directory is usable. A second call
 * keeps the first sample so a later rewrite of the file cannot change it. */
static void load_limits(zk_power_t *p)
{
    char path[ZK_POWER_DIR_MAX + 32];
    int v;

    if (p->limits_ready) {
        return;
    }
    p->limits_ready = 1;
    p->max_brightness = 255;
    if (join_path(path, sizeof path, p->dir, "max_brightness") == 0 && read_int_file(path, &v) == 0 &&
        v >= 1 && v <= ZK_POWER_MAX_BR_CAP) {
        p->max_brightness = v;
    }
    if (join_path(path, sizeof path, p->dir, "brightness") == 0 && read_int_file(path, &v) == 0) {
        p->saved_brightness = v;
        p->have_saved = 1;
    }
}

/* 0: do not write (already dark). Otherwise *out is never brighter than the
 * saved original, and never below 1 when that original was at least 1. */
static int dim_brightness(const zk_power_t *p, int *out)
{
    int level;

    if (p->have_saved && p->saved_brightness < 1) {
        return 0;
    }
    level = p->dim_level;
    if (level < 1) {
        level = 1;
    }
    if (p->max_brightness >= 1 && level > p->max_brightness) {
        level = p->max_brightness;
    }
    if (p->have_saved && level > p->saved_brightness) {
        level = p->saved_brightness;
    }
    if (level < 1) {
        level = 1;
    }
    if (out) {
        *out = level;
    }
    return 1;
}

static int active_write_level(const zk_power_t *p)
{
    int v;

    if (p->have_saved && p->saved_brightness >= 1) {
        v = p->saved_brightness;
    } else {
        v = p->max_brightness >= 1 ? p->max_brightness : 255;
    }
    if (p->max_brightness >= 1 && v > p->max_brightness) {
        v = p->max_brightness;
    }
    if (v < 1) {
        v = p->max_brightness >= 1 ? p->max_brightness : 1;
    }
    return v;
}

static int write_brightness(zk_power_t *p, int value)
{
    char path[ZK_POWER_DIR_MAX + 32];
    char num[16];

    if (!p->bl_ok) {
        return -1;
    }
    if (join_path(path, sizeof path, p->dir, "brightness") != 0) {
        p->bl_ok = 0;
        log_once(&p->logged_br, "power: brightness not writable");
        return -1;
    }
    if (write_int_file(path, value) != 0) {
        p->bl_ok = 0;
        if (!p->logged_br) {
            p->logged_br = 1;
            fprintf(stderr, "power: %s: %s\n", path, strerror(errno));
        }
        return -1;
    }
    snprintf(num, sizeof num, "%d", value);
    trace_add(p, "brightness", num);
    p->hw_touched = 1;
    return 0;
}

static int write_bl_power(zk_power_t *p, int value)
{
    char path[ZK_POWER_DIR_MAX + 32];
    char num[16];
    struct stat st;

    if (join_path(path, sizeof path, p->dir, "bl_power") != 0) {
        return -1;
    }
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    if (write_int_file(path, value) != 0) {
        if (!p->logged_bl) {
            p->logged_bl = 1;
            fprintf(stderr, "power: %s: %s\n", path, strerror(errno));
        }
        return -1;
    }
    snprintf(num, sizeof num, "%d", value);
    trace_add(p, "bl_power", num);
    p->hw_touched = 1;
    return 0;
}

static void blank_down(zk_power_t *p)
{
    int rc;

    if (!p->fb_real || !p->blank || p->blanked) {
        return;
    }
    rc = p->blank(p->blank_ctx, 1);
    if (rc == 0) {
        p->blanked = 1;
        trace_add(p, "blank", "1");
    } else {
        log_once(&p->logged_blank, "power: blank failed");
    }
}

static void blank_up(zk_power_t *p)
{
    int rc;

    if (!p->blanked || !p->blank) {
        return;
    }
    rc = p->blank(p->blank_ctx, 0);
    if (rc == 0) {
        p->blanked = 0;
        trace_add(p, "blank", "0");
    } else {
        log_once(&p->logged_blank, "power: unblank failed");
    }
}

static void apply(zk_power_t *p, zk_power_state_t next)
{
    if (!p->bl_ok) {
        if (p->bl_bad) {
            log_once(&p->logged_br, "power: brightness not writable");
        }
        if (next == ZK_POWER_OFF) {
            blank_down(p);
        } else {
            blank_up(p);
        }
        return;
    }
    if (next == ZK_POWER_OFF) {
        if (write_brightness(p, 0) != 0) {
            blank_down(p);
            return;
        }
        write_bl_power(p, 1);
        return;
    }
    if (next == ZK_POWER_DIMMED) {
        int level = 0;

        if (p->state == ZK_POWER_OFF) {
            write_bl_power(p, 0);
        }
        if (dim_brightness(p, &level)) {
            write_brightness(p, level);
        }
        return;
    }
    if (p->state != ZK_POWER_ACTIVE) {
        write_bl_power(p, 0);
        write_brightness(p, active_write_level(p));
    }
}

static void set_state(zk_power_t *p, zk_power_state_t next)
{
    if (p->state == next) {
        return;
    }
    apply(p, next);
    p->state = next;
    p->transitions++;
}

static int blocked(const zk_power_inputs_t *in)
{
    return in->watering || in->lockout || in->fault || in->unreachable || in->modal_open;
}

void zk_power_init(zk_power_t *p, const zk_power_config_t *cfg)
{
    int dim;
    int off;
    int level;
    const char *dir;

    if (!p) {
        return;
    }
    memset(p, 0, sizeof *p);
    p->state = ZK_POWER_ACTIVE;
    p->max_brightness = 255;
    dir = NULL;
    dim = 0;
    if (cfg) {
        dim = cfg->dim_after_sec;
        dir = cfg->backlight_dir;
    }
    copy_dir(p, dir);
    /* Disabled dimming must not stat or read the backlight directory. */
    if (!cfg || dim <= 0) {
        p->enabled = 0;
        return;
    }
    if (dim > ZK_POWER_SEC_CAP) {
        dim = ZK_POWER_SEC_CAP;
    }
    off = cfg->off_after_sec;
    if (off < 0) {
        off = 0;
    }
    if (off > ZK_POWER_SEC_CAP) {
        off = ZK_POWER_SEC_CAP;
    }
    if (off > 0 && off <= dim) {
        log_once(&p->logged_off, "power: off_after is not greater than dim_after; OFF disabled");
        off = 0;
    }
    level = cfg->dim_level;
    if (level < 0) {
        level = 0;
    }
    if (level > 255) {
        level = 255;
    }
    p->dim_after_sec = dim;
    p->off_after_sec = off;
    p->dim_level = level;
    p->dim_ms = (int64_t)dim * 1000;
    p->off_ms = (int64_t)off * 1000;
    p->fb_real = cfg->fb_real ? 1 : 0;
    p->blank = cfg->blank;
    p->blank_ctx = cfg->blank_ctx;
    p->enabled = 1;
    probe_backlight(p);
    if (p->bl_ok) {
        load_limits(p);
    }
}

void zk_power_tick(zk_power_t *p, int64_t now_ms, const zk_power_inputs_t *in)
{
    int64_t idle;

    if (!p || p->shut || !p->enabled) {
        return;
    }
    if (!in) {
        return;
    }
    if (!p->anchored) {
        p->last_ms = now_ms;
        p->anchored = 1;
    }
    if (blocked(in)) {
        p->last_ms = now_ms;
        set_state(p, ZK_POWER_ACTIVE);
        return;
    }
    if (p->state == ZK_POWER_OFF && in->paused) {
        set_state(p, ZK_POWER_DIMMED);
    }
    idle = now_ms >= p->last_ms ? now_ms - p->last_ms : 0;
    if (p->off_ms > 0 && !in->paused && idle >= p->off_ms) {
        set_state(p, ZK_POWER_OFF);
    } else if (idle >= p->dim_ms) {
        set_state(p, ZK_POWER_DIMMED);
    } else {
        set_state(p, ZK_POWER_ACTIVE);
    }
}

zk_power_touch_action_t zk_power_touch_event(zk_power_t *p, int64_t now_ms, int pressed, int hit_is_stop)
{
    int edge_down;
    int edge_up;

    if (!p || !p->enabled || p->shut) {
        return ZK_POWER_PASS;
    }
    pressed = pressed ? 1 : 0;
    /* LVGL polls released every frame. That is not a touch and must not reset idle. */
    if (!pressed && !p->finger) {
        return ZK_POWER_PASS;
    }
    edge_down = pressed && !p->finger;
    edge_up = !pressed && p->finger;
    p->finger = pressed;
    if (p->guard_until > 0 && now_ms < p->guard_until) {
        if (edge_down) {
            p->contact_swallow = 1;
        }
        if (p->contact_swallow && edge_up) {
            p->contact_swallow = 0;
        }
        return ZK_POWER_SWALLOW;
    }
    if (edge_down) {
        if (p->state == ZK_POWER_OFF) {
            p->contact_swallow = 1;
            set_state(p, ZK_POWER_ACTIVE);
        } else if (p->state == ZK_POWER_DIMMED && hit_is_stop) {
            p->contact_swallow = 0;
            set_state(p, ZK_POWER_ACTIVE);
        } else if (p->state == ZK_POWER_DIMMED) {
            p->contact_swallow = 1;
            set_state(p, ZK_POWER_ACTIVE);
        } else {
            p->contact_swallow = 0;
        }
        p->last_ms = now_ms;
        p->anchored = 1;
    }
    if (p->contact_swallow) {
        if (edge_up) {
            p->guard_until = now_ms + (int64_t)ZK_POWER_WAKE_GUARD_MS;
            p->contact_swallow = 0;
        }
        return ZK_POWER_SWALLOW;
    }
    p->last_ms = now_ms;
    p->anchored = 1;
    return ZK_POWER_PASS;
}

void zk_power_shutdown(zk_power_t *p)
{
    if (!p || p->shut) {
        return;
    }
    p->shut = 1;
    if (!p->enabled) {
        return;
    }
    if (p->state != ZK_POWER_ACTIVE) {
        set_state(p, ZK_POWER_ACTIVE);
    } else if (p->bl_ok && p->hw_touched) {
        write_bl_power(p, 0);
        write_brightness(p, active_write_level(p));
    }
    if (p->blanked) {
        blank_up(p);
    }
}

int zk_power_enabled(const zk_power_t *p)
{
    return p && p->enabled && !p->shut;
}

zk_power_state_t zk_power_state(const zk_power_t *p)
{
    return p ? p->state : ZK_POWER_ACTIVE;
}

int zk_power_transitions(const zk_power_t *p)
{
    return p ? p->transitions : 0;
}

int zk_power_dim_after_sec(const zk_power_t *p)
{
    return p ? p->dim_after_sec : 0;
}

int zk_power_off_after_sec(const zk_power_t *p)
{
    return p ? p->off_after_sec : 0;
}

int zk_power_dim_level(const zk_power_t *p)
{
    return p ? p->dim_level : 0;
}

const char *zk_power_backlight_dir(const zk_power_t *p)
{
    return p ? p->dir : "";
}

int zk_power_trace_len(const zk_power_t *p)
{
    return p ? p->trace_n : 0;
}

int zk_power_trace_at(const zk_power_t *p, int index, const char **what, const char **value)
{
    if (!p || index < 0 || index >= p->trace_n) {
        return 0;
    }
    if (what) {
        *what = p->trace[index].what;
    }
    if (value) {
        *value = p->trace[index].value;
    }
    return 1;
}
