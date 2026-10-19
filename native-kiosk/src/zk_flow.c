#include "zk_flow.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

void zk_flow_tap(enum zk_target_id id, const zk_flow_tile_t *tile, zk_flow_out_t *out)
{
    int on = tile && tile->tile_on;
    int paused = tile && tile->paused;
    if (!out) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (id >= ZK_TARGET_TILE0 && id <= ZK_TARGET_TILE5) {
        if (on) {
            out->effect = ZK_TAP_OPEN_STOP_STATION;
        } else if (paused) {
            out->effect = ZK_TAP_OPEN_ANYWAY;
        } else {
            out->effect = ZK_TAP_OPEN_RUN;
        }
        return;
    }
    switch (id) {
    case ZK_TARGET_STOP:
        out->effect = ZK_TAP_OPEN_STOP_ALL;
        break;
    case ZK_TARGET_RUN:
        out->effect = ZK_TAP_SUBMIT_RUN;
        break;
    case ZK_TARGET_STOP_ALL:
        out->effect = ZK_TAP_SUBMIT_STOP;
        break;
    case ZK_TARGET_PAUSE:
        out->effect = ZK_TAP_SUBMIT_PAUSE;
        break;
    case ZK_TARGET_RESUME:
        out->effect = ZK_TAP_SUBMIT_RESUME;
        break;
    case ZK_TARGET_CANCEL:
    case ZK_TARGET_OK:
        out->effect = ZK_TAP_CLOSE;
        break;
    case ZK_TARGET_HERO:
        out->effect = ZK_TAP_OPEN_OVERLAP;
        break;
    case ZK_TARGET_MINUS:
        out->effect = ZK_TAP_STEPPER;
        out->minutes_delta = -1;
        break;
    case ZK_TARGET_PLUS:
        out->effect = ZK_TAP_STEPPER;
        out->minutes_delta = 1;
        break;
    case ZK_TARGET_SCHEDULES:
        out->effect = ZK_TAP_OPEN_SCHEDULES;
        break;
    case ZK_TARGET_HOME:
        out->effect = ZK_TAP_HOME;
        break;
    case ZK_TARGET_SCHED_NEXT:
        out->effect = ZK_TAP_SCHED_NEXT;
        break;
    default:
        break;
    }
}

int zk_flow_debounce_ok(int64_t last_ms, int64_t now_ms)
{
    if (last_ms <= 0) {
        return 1;
    }
    if (now_ms < last_ms) {
        return 1;
    }
    return (now_ms - last_ms) >= (int64_t)ZK_DEBOUNCE_MS;
}

static int idle_screen(enum zk_screen screen)
{
    switch (screen) {
    case ZK_SCREEN_SCHEDULES:
    case ZK_SCREEN_SHEET_RUN:
    case ZK_SCREEN_SHEET_ANYWAY:
    case ZK_SCREEN_SHEET_STOP_STATION:
    case ZK_SCREEN_SHEET_OVERLAP:
    case ZK_SCREEN_SHEET_STOP_ALL:
        return 1;
    default:
        return 0;
    }
}

int64_t zk_idle_home_ms(void)
{
    const char *env;
    char *end;
    long v;
    env = getenv("ZK_IDLE_HOME_MS");
    if (!env || !env[0]) {
        return (int64_t)ZK_IDLE_HOME_MS;
    }
    errno = 0;
    v = strtol(env, &end, 10);
    if (errno != 0 || end == env || *end != '\0' || v < 1 || v > 600000L) {
        return (int64_t)ZK_IDLE_HOME_MS;
    }
    return (int64_t)v;
}

int zk_flow_idle_returns_home_limit(enum zk_screen screen, int64_t idle_ms, int64_t limit_ms)
{
    if (limit_ms < 1) {
        limit_ms = (int64_t)ZK_IDLE_HOME_MS;
    }
    if (idle_ms < limit_ms) {
        return 0;
    }
    return idle_screen(screen);
}

int zk_flow_idle_returns_home(enum zk_screen screen, int64_t idle_ms)
{
    return zk_flow_idle_returns_home_limit(screen, idle_ms, (int64_t)ZK_IDLE_HOME_MS);
}

int zk_stop_confirm(zk_stop_flow_t *f)
{
    if (!f) {
        return 0;
    }
    if (f->inflight) {
        f->again = 1;
        return 0;
    }
    /* A second confirm always sends. One POST at a time. */
    f->phase = ZK_STOP_QUEUED;
    f->inflight = 1;
    f->again = 0;
    f->failed = 0;
    return 1;
}

void zk_stop_http(zk_stop_flow_t *f, int ok)
{
    if (!f || f->phase != ZK_STOP_QUEUED) {
        return;
    }
    f->inflight = 0;
    if (ok) {
        f->phase = ZK_STOP_WAIT;
        f->failed = 0;
        return;
    }
    f->failed = 1;
}

int zk_stop_snapshot(zk_stop_flow_t *f, zk_stop_snap_t snap)
{
    if (!f) {
        return 0;
    }
    if (f->phase != ZK_STOP_WAIT && f->phase != ZK_STOP_QUEUED) {
        return 0;
    }
    if (f->inflight) {
        return 0;
    }
    if (snap == ZK_STOP_SNAP_CLEAR) {
        f->phase = ZK_STOP_CONFIRMED;
        f->inflight = 0;
        f->again = 0;
        f->failed = 0;
        return 0;
    }
    /* Still on, or no snapshot: stay pending. The retry timer owns the POST. */
    if (f->phase == ZK_STOP_WAIT) {
        f->phase = ZK_STOP_QUEUED;
        f->inflight = 0;
        f->again = 0;
        f->failed = 1;
        return 0;
    }
    return 0;
}

void zk_stop_mark_sent(zk_stop_flow_t *f, int64_t now_ms)
{
    if (!f) {
        return;
    }
    f->sent_ms = now_ms;
    f->sent_set = 1;
}

int zk_stop_retry_due(const zk_stop_flow_t *f, int64_t now_ms)
{
    if (!f || f->phase != ZK_STOP_QUEUED || f->inflight) {
        return 0;
    }
    if (!f->sent_set || now_ms < f->sent_ms) {
        return 1;
    }
    return (now_ms - f->sent_ms) >= (int64_t)ZK_STOP_RETRY_MS;
}

int zk_stop_retry(zk_stop_flow_t *f)
{
    if (!f || f->phase != ZK_STOP_QUEUED || f->inflight) {
        return 0;
    }
    f->inflight = 1;
    return 1;
}

int zk_stop_shows_stopped(const zk_stop_flow_t *f)
{
    return f && f->phase == ZK_STOP_CONFIRMED;
}
