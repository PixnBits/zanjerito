#ifndef ZK_FLOW_H
#define ZK_FLOW_H

#include "zk_layout.h"

#include <stdint.h>

#define ZK_DEBOUNCE_MS 300

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
    ZK_TAP_SUBMIT_RESUME
} zk_tap_effect_t;

typedef struct {
    int paused;
    int tile_on; /* the tapped station is running */
} zk_flow_tile_t;

typedef struct {
    zk_tap_effect_t effect;
    int minutes_delta; /* stepper: -1 or +1 */
} zk_flow_out_t;

/* A tile never returns ZK_TAP_SUBMIT_RUN. Stop-station closes without a POST. */
void zk_flow_tap(enum zk_target_id id, const zk_flow_tile_t *tile, zk_flow_out_t *out);

/* 1 when at least ZK_DEBOUNCE_MS has passed since the previous accepted release.
 * last_ms 0 means no previous tap. A sample at exactly +300 ms is accepted. */
int zk_flow_debounce_ok(int64_t last_ms, int64_t now_ms);

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
    int inflight;
} zk_stop_flow_t;

/* 1 when the caller should POST /api/run/cancel once.
 * Confirming again while queued or waiting does not return 1. */
int zk_stop_confirm(zk_stop_flow_t *f);
void zk_stop_http(zk_stop_flow_t *f, int ok);
/* Ignored unless the phase is WAIT. Does not show stopped on HTTP ok alone. */
void zk_stop_snapshot(zk_stop_flow_t *f, zk_stop_snap_t snap);
/* 1 when a failed request should be posted again. Sets inflight. */
int zk_stop_retry(zk_stop_flow_t *f);
int zk_stop_shows_stopped(const zk_stop_flow_t *f);

#endif
