#ifndef ZK_FLOW_H
#define ZK_FLOW_H

#include "zk_layout.h"

#include <stdint.h>

#define ZK_DEBOUNCE_MS 300
/* No touch for this long leaves Schedules and every sheet, including Stop all. */
#define ZK_IDLE_HOME_MS 60000

typedef enum {
    ZK_TAP_NONE = 0,
    ZK_TAP_OPEN_RUN,
    ZK_TAP_OPEN_ANYWAY,
    ZK_TAP_OPEN_STOP_STATION,
    ZK_TAP_OPEN_STOP_ALL,
    ZK_TAP_OPEN_OVERLAP,
    ZK_TAP_CLOSE,
    ZK_TAP_STEPPER,
    ZK_TAP_SUBMIT_RUN,
    ZK_TAP_SUBMIT_STOP,
    ZK_TAP_SUBMIT_PAUSE,
    ZK_TAP_SUBMIT_RESUME,
    ZK_TAP_OPEN_SCHEDULES,
    ZK_TAP_HOME,
    ZK_TAP_SCHED_NEXT
} zk_tap_effect_t;

typedef struct {
    int paused;
    int tile_on; /* the tapped station is running */
} zk_flow_tile_t;

typedef struct {
    zk_tap_effect_t effect;
    int minutes_delta; /* stepper: -1 or +1 */
} zk_flow_out_t;

/* A tile never returns ZK_TAP_SUBMIT_RUN or ZK_TAP_SUBMIT_STOP. Cancel closes without a POST. */
void zk_flow_tap(enum zk_target_id id, const zk_flow_tile_t *tile, zk_flow_out_t *out);

/* 1 when at least ZK_DEBOUNCE_MS has passed since the previous accepted release.
 * last_ms 0 means no previous tap. A sample at exactly +300 ms is accepted. */
int zk_flow_debounce_ok(int64_t last_ms, int64_t now_ms);

/* 1 when this screen should return to Home after idle_ms with no touch.
 * Stop all is included. The caller must not confirm the stop. Home returns 0. */
int zk_flow_idle_returns_home(enum zk_screen screen, int64_t idle_ms);

typedef enum {
    ZK_STOP_IDLE = 0,
    ZK_STOP_QUEUED, /* one request in flight or waiting to retry */
    ZK_STOP_WAIT,   /* cancel returned 2xx; waiting for a snapshot decision */
    ZK_STOP_CONFIRMED
} zk_stop_phase_t;

typedef enum {
    ZK_STOP_SNAP_CLEAR = 0,
    ZK_STOP_SNAP_STILL_ON,
    ZK_STOP_SNAP_UNAVAILABLE
} zk_stop_snap_t;

typedef struct {
    zk_stop_phase_t phase;
    int inflight; /* a cancel POST is on the wire right now */
    int again;    /* user confirmed again while that POST was in flight */
    int failed;   /* the last attempt failed; a retry owns the next POST */
} zk_stop_flow_t;

/* 1 when the caller should POST /api/run/cancel once.
 * An in-flight POST returns 0 and remembers the confirm in again.
 * Any other phase, including QUEUED while waiting to retry, sends again.
 * One POST is in flight at a time. */
int zk_stop_confirm(zk_stop_flow_t *f);
void zk_stop_http(zk_stop_flow_t *f, int ok);
/* HTTP 2xx is not the ack. Only ZK_STOP_SNAP_CLEAR (nothing on) confirms.
 * STILL_ON and UNAVAILABLE from WAIT re-queue one POST and return 1.
 * QUEUED and not in flight confirms on CLEAR; otherwise the retry timer owns
 * the resend and this returns 0. An in-flight POST is not a decision.
 * Does not show stopped on HTTP ok alone, and does not confirm when no
 * snapshot is available. */
int zk_stop_snapshot(zk_stop_flow_t *f, zk_stop_snap_t snap);
/* 1 when a failed request should be posted again. Sets inflight. */
int zk_stop_retry(zk_stop_flow_t *f);
int zk_stop_shows_stopped(const zk_stop_flow_t *f);

#endif
