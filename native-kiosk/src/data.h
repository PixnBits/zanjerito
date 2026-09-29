#ifndef ZK_DATA_H
#define ZK_DATA_H

#include "app.h"
#include "zk_model.h"

#include <stdint.h>

typedef struct {
    zk_status_t status;
    int have_status;
    zk_stations_t stations;
    zk_schedules_t schedules;
    zk_soil_t soil;
    int have_stations, have_schedules, have_soil;
    double status_age_s; /* seconds since last successful status poll (large if never) */
    int stale; /* status_age_s > 6 */
    zk_wall_t wall; /* status.now advanced by monotonic delta since that poll (frozen at status.now in fixture mode unless app->live_clock) */
    uint32_t version; /* increments whenever any snapshot content changes or the wall minute/second changes when requested */
} zk_snapshot_t;

int zk_data_start(const zk_app_t *app); /* fixture mode: load status/stations/schedules/soil .json from app->fixture_dir once, no thread needed; HTTP mode: start worker: GET /api/status every 2 s, GET /api/stations, /api/schedules, /api/soil every 30 s (all immediately at start); read-only GETs only */
void zk_data_stop(void);
void zk_data_debug_force_stale(int on); /* fixture mode: show the offline pill (shots) */
void zk_data_get(zk_snapshot_t *out); /* copy under mutex; computes wall/age/stale */
int zk_data_wake_fd(void); /* pipe read fd that becomes readable when new data arrives; pair with zk_platform_wake() if easier: call zk_platform_wake() from the worker when the snapshot changes */

/* async actions, executed on a small worker queue so the UI thread never blocks: */
typedef enum { ZK_ACT_STOP, ZK_ACT_PAUSE, ZK_ACT_RESUME } zk_act_kind_t;
void zk_data_submit_action(zk_act_kind_t kind, const char *json_body /* pause only */);

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
