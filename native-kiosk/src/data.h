#ifndef ZK_DATA_H
#define ZK_DATA_H

#include "app.h"
#include "zk_extra.h"
#include "zk_model.h"

#include <stdint.h>

typedef enum {
    ZK_API_OK = 0,
    ZK_API_NEEDS_UPDATE,
    ZK_API_UNREACHABLE
} zk_api_state_t;

typedef struct {
    zk_kiosk_t kiosk;
    int have_kiosk;
    zk_schedules_t schedules;
    int have_schedules;
    zk_api_state_t api_state;
    zk_soil_doc_t soil_doc;
    int have_soil_doc;
    zk_last_run_t lasts[ZK_MAX_STATIONS];
    int n_lasts;
    double status_age_s; /* seconds since last successful kiosk poll (large if never) */
    int stale; /* status_age_s > 6 */
    zk_wall_t wall; /* kiosk.now advanced by monotonic delta since that poll (frozen at kiosk.now in fixture mode unless app->live_clock) */
    uint32_t version; /* increments whenever any snapshot content changes or the wall minute/second changes when requested */
    uint32_t kiosk_gen; /* increments on every successful /api/kiosk parse, including an identical body */
} zk_snapshot_t;

/* Fixture mode loads kiosk.json + schedules.json once, plus soil.json and
 * history.json when those files are present. No thread and no socket.
 * HTTP mode starts two threads. The poll thread GETs /api/kiosk every 2 s.
 * Every 30 s it also GETs /api/soil, /api/history, and /api/schedules.
 * A failed soil or history GET does not mark the controller unreachable.
 * The action thread POSTs on its own sockets and does not wait on those GETs.
 * HTTP 404 from /api/kiosk is NEEDS_UPDATE and does not fall back to /api/status. */
int zk_data_start(const zk_app_t *app);
void zk_data_stop(void);
void zk_data_debug_force_stale(int on); /* fixture mode: show the offline pill (shots) */
void zk_data_debug_force_needs_update(int on); /* shots: controller-needs-update card */
void zk_data_get(zk_snapshot_t *out); /* copy under mutex; computes wall/age/stale */
int zk_data_wake_fd(void);

/* Async, on the action thread, so the UI thread never blocks.
 * A STOP or resume already queued or in flight is not posted again.
 * A pause is skipped only when the same body is already queued or in flight.
 * The result the UI reads is that one request (latest seq). */
typedef enum { ZK_ACT_STOP, ZK_ACT_PAUSE, ZK_ACT_RESUME, ZK_ACT_RUN } zk_act_kind_t;
/* Pause body is JSON. Run body is "<station-id>\\n{json}" from zk_action_run_body. */
void zk_data_submit_action(zk_act_kind_t kind, const char *json_body);

/* result polling: */
typedef struct {
    int pending;
    int done;
    int code; /* ZK_OK, ZK_RO, or negative/HTTP error */
    int http_status;
    zk_act_kind_t kind;
    uint32_t seq;
} zk_action_result_t;
void zk_data_action_result(zk_action_result_t *out);

#endif
