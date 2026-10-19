#include "zk_extra.h"

#include "cJSON.h"

#include <string.h>

static int outcome_counts(const char *s)
{
    return s && (strcmp(s, "completed") == 0 || strcmp(s, "stopped") == 0);
}

int zk_parse_soil_doc(const char *json, zk_soil_doc_t *out)
{
    cJSON *root;
    cJSON *en;
    cJSON *stale;
    cJSON *known;
    cJSON *reason;
    cJSON *updated;
    cJSON *zones;
    cJSON *z;
    if (!out) {
        return ZK_ERR_ARG;
    }
    memset(out, 0, sizeof *out);
    if (!json) {
        return ZK_ERR_ARG;
    }
    root = cJSON_Parse(json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ZK_ERR_PARSE;
    }
    en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    stale = cJSON_GetObjectItemCaseSensitive(root, "et_stale");
    known = cJSON_GetObjectItemCaseSensitive(root, "et_known");
    reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
    updated = cJSON_GetObjectItemCaseSensitive(root, "updated_at");
    out->enabled = cJSON_IsTrue(en) ? 1 : 0;
    out->et_stale = cJSON_IsTrue(stale) ? 1 : 0;
    out->et_known = cJSON_IsTrue(known) ? 1 : 0;
    if (cJSON_IsString(reason) && reason->valuestring) {
        zk_str_copy(out->reason, sizeof out->reason, reason->valuestring);
    }
    if (cJSON_IsString(updated) && updated->valuestring && updated->valuestring[0]) {
        if (zk_parse_wall(updated->valuestring, &out->updated_at) == ZK_OK) {
            out->has_updated = 1;
        }
    }
    if (strstr(out->reason, "no soil.local.json") != NULL) {
        out->configured = 0;
    } else if (out->enabled || strstr(out->reason, "soil.local.json invalid") != NULL) {
        out->configured = 1;
    } else {
        out->configured = 0;
    }
    zones = cJSON_GetObjectItemCaseSensitive(root, "zones");
    if (cJSON_IsArray(zones)) {
        for (z = zones->child; z && out->n_zones < ZK_MAX_STATIONS; z = z->next) {
            cJSON *id;
            cJSON *pct;
            cJSON *rate;
            zk_soil_zone_t *zone;
            if (!cJSON_IsObject(z)) {
                continue;
            }
            id = cJSON_GetObjectItemCaseSensitive(z, "station_id");
            if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0]) {
                continue;
            }
            zone = &out->zones[out->n_zones];
            zk_str_copy(zone->id, sizeof zone->id, id->valuestring);
            pct = cJSON_GetObjectItemCaseSensitive(z, "percent");
            rate = cJSON_GetObjectItemCaseSensitive(z, "rate_measured");
            zone->rate_measured = cJSON_IsTrue(rate) ? 1 : 0;
            {
                cJSON *age = cJSON_GetObjectItemCaseSensitive(z, "age_days");
                if (cJSON_IsNumber(age) && age->valueint >= 0 && age->valueint < 10000) {
                    zone->has_age = 1;
                    zone->age_days = age->valueint;
                }
            }
            if (cJSON_IsNumber(pct)) {
                int p = pct->valueint;
                if (p < 0) {
                    p = 0;
                }
                if (p > 100) {
                    p = 100;
                }
                zone->has_percent = 1;
                zone->percent = p;
            }
            out->n_zones++;
        }
    }
    cJSON_Delete(root);
    return ZK_OK;
}

static int already(const zk_last_run_t *out, int n, const char *id)
{
    int i;
    for (i = 0; i < n; i++) {
        if (strcmp(out[i].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void take_entry(cJSON *e, zk_last_run_t *out, int max, int *n)
{
    cJSON *outcome;
    cJSON *started;
    cJSON *stations;
    cJSON *st;
    zk_wall_t wall;
    if (!cJSON_IsObject(e) || !out || !n || *n >= max) {
        return;
    }
    outcome = cJSON_GetObjectItemCaseSensitive(e, "outcome");
    if (!cJSON_IsString(outcome) || !outcome_counts(outcome->valuestring)) {
        return;
    }
    started = cJSON_GetObjectItemCaseSensitive(e, "started_at");
    if (!cJSON_IsString(started) || zk_parse_wall(started->valuestring, &wall) != ZK_OK) {
        return;
    }
    if (wall.wday < 0 || wall.wday > 6) {
        zk_wall_set_wday(&wall);
    }
    stations = cJSON_GetObjectItemCaseSensitive(e, "stations");
    if (!cJSON_IsArray(stations)) {
        return;
    }
    for (st = stations->child; st && *n < max; st = st->next) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(st, "station_id");
        if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0]) {
            continue;
        }
        if (already(out, *n, id->valuestring)) {
            continue;
        }
        zk_str_copy(out[*n].id, sizeof out[*n].id, id->valuestring);
        out[*n].wday = wall.wday;
        (*n)++;
    }
}

int zk_parse_history_lasts(const char *json, zk_last_run_t *out, int max)
{
    cJSON *root;
    cJSON *entries;
    cJSON *e;
    int n = 0;
    if (!out || max <= 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out) * (size_t)max);
    if (!json) {
        return 0;
    }
    root = cJSON_Parse(json);
    if (!root) {
        return 0;
    }
    if (cJSON_IsArray(root)) {
        entries = root;
    } else {
        entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    }
    if (cJSON_IsArray(entries)) {
        for (e = entries->child; e && n < max; e = e->next) {
            take_entry(e, out, max, &n);
        }
    }
    cJSON_Delete(root);
    return n;
}
