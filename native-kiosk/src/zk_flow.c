#include "zk_flow.h"

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

int zk_flow_idle_returns_home(enum zk_screen screen, int64_t idle_ms)
{
    if (idle_ms < (int64_t)ZK_IDLE_HOME_MS) {
        return 0;
    }
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
    /* Still on, or no snapshot: stay pending. From WAIT, send again now. */
    if (f->phase == ZK_STOP_WAIT) {
        f->phase = ZK_STOP_QUEUED;
        f->inflight = 1;
        f->again = 0;
        f->failed = 0;
        return 1;
    }
    return 0;
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
