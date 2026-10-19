#ifndef ZK_EXTRA_H
#define ZK_EXTRA_H

#include "zk_model.h"

/* GET /api/soil and GET /api/history, parsed for the kiosk. No new API fields. */

typedef struct {
    char id[ZK_ID_MAX];
    int has_percent;
    int percent;
    int rate_measured;
    /* Optional display hint. The controller does not send this. When present
     * and >= 2, this zone is stale on its own. Live responses use the shared
     * ET timestamp instead. */
    int has_age;
    int age_days;
} zk_soil_zone_t;

typedef struct {
    int configured; /* settings file exists (enabled, or invalid file) */
    int enabled;
    int et_stale;
    int et_known;
    int has_updated;
    zk_wall_t updated_at;
    char reason[96];
    zk_soil_zone_t zones[ZK_MAX_STATIONS];
    int n_zones;
} zk_soil_doc_t;

typedef struct {
    char id[ZK_ID_MAX];
    int wday; /* 0=Sun .. 6=Sat */
} zk_last_run_t;

int zk_parse_soil_doc(const char *json, zk_soil_doc_t *out);
/* Newest completed or stopped run per station. Returns the count. */
int zk_parse_history_lasts(const char *json, zk_last_run_t *out, int max);

#endif
