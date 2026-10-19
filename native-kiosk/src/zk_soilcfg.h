#ifndef ZK_SOILCFG_H
#define ZK_SOILCFG_H

#include "zk_model.h"

/* Placeholder Dry/OK/Wet cuts. Display only. Defaults 30/80.
 * File (ZAN_SOIL_THRESHOLDS, else /etc/zan-kiosk-soil.json if it exists)
 * then ZAN_SOIL_DRY_LT / ZAN_SOIL_WET_GT override the global cuts only. */

typedef struct {
    char id[ZK_ID_MAX];
    int dry_lt;
    int wet_gt;
} zk_soil_cut_t;

typedef struct {
    int dry_lt;
    int wet_gt;
    zk_soil_cut_t stations[ZK_MAX_STATIONS];
    int n_stations;
} zk_soil_cfg_t;

void zk_soil_cfg_defaults(zk_soil_cfg_t *cfg);
/* 0 on success, including a missing file (defaults stay). */
int zk_soil_cfg_load(zk_soil_cfg_t *cfg, const char *path_or_null);
void zk_soil_cfg_apply_env(zk_soil_cfg_t *cfg);
const char *zk_soil_word(const zk_soil_cfg_t *cfg, const char *station_id, int percent);

#endif
