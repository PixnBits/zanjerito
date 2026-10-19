#include "zk_soilcfg.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void zk_soil_cfg_defaults(zk_soil_cfg_t *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof *cfg);
    cfg->dry_lt = 30;
    cfg->wet_gt = 80;
}

static int clamp_cut(int v, int fallback)
{
    if (v < 0 || v > 100) {
        return fallback;
    }
    return v;
}

static void read_file(zk_soil_cfg_t *cfg, const char *path)
{
    FILE *f;
    long n;
    char *buf;
    cJSON *root;
    cJSON *dry;
    cJSON *wet;
    cJSON *stations;
    cJSON *st;
    if (!path || !path[0]) {
        return;
    }
    f = fopen(path, "rb");
    if (!f) {
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    n = ftell(f);
    if (n < 0 || n > 65536) {
        fclose(f);
        return;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return;
    }
    buf[n] = 0;
    fclose(f);
    root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return;
    }
    dry = cJSON_GetObjectItemCaseSensitive(root, "dry_lt");
    wet = cJSON_GetObjectItemCaseSensitive(root, "wet_gt");
    if (cJSON_IsNumber(dry)) {
        cfg->dry_lt = clamp_cut(dry->valueint, cfg->dry_lt);
    }
    if (cJSON_IsNumber(wet)) {
        cfg->wet_gt = clamp_cut(wet->valueint, cfg->wet_gt);
    }
    stations = cJSON_GetObjectItemCaseSensitive(root, "stations");
    if (cJSON_IsObject(stations)) {
        for (st = stations->child; st && cfg->n_stations < ZK_MAX_STATIONS; st = st->next) {
            zk_soil_cut_t *cut;
            cJSON *sd;
            cJSON *sw;
            if (!st->string || !st->string[0]) {
                continue;
            }
            cut = &cfg->stations[cfg->n_stations];
            zk_str_copy(cut->id, sizeof cut->id, st->string);
            cut->dry_lt = cfg->dry_lt;
            cut->wet_gt = cfg->wet_gt;
            sd = cJSON_GetObjectItemCaseSensitive(st, "dry_lt");
            sw = cJSON_GetObjectItemCaseSensitive(st, "wet_gt");
            if (cJSON_IsNumber(sd)) {
                cut->dry_lt = clamp_cut(sd->valueint, cut->dry_lt);
            }
            if (cJSON_IsNumber(sw)) {
                cut->wet_gt = clamp_cut(sw->valueint, cut->wet_gt);
            }
            cfg->n_stations++;
        }
    }
    cJSON_Delete(root);
}

static int env_int(const char *name, int *out)
{
    const char *s = getenv(name);
    char *end = NULL;
    long v;
    if (!s || !s[0]) {
        return 0;
    }
    v = strtol(s, &end, 10);
    if (!end || end == s || *end != 0) {
        return 0;
    }
    if (v < 0 || v > 100) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

void zk_soil_cfg_apply_env(zk_soil_cfg_t *cfg)
{
    int v;
    if (!cfg) {
        return;
    }
    if (env_int("ZAN_SOIL_DRY_LT", &v)) {
        cfg->dry_lt = v;
    }
    if (env_int("ZAN_SOIL_WET_GT", &v)) {
        cfg->wet_gt = v;
    }
}

int zk_soil_cfg_load(zk_soil_cfg_t *cfg, const char *path_or_null)
{
    const char *path = path_or_null;
    if (!cfg) {
        return ZK_ERR_ARG;
    }
    zk_soil_cfg_defaults(cfg);
    if (!path) {
        path = getenv("ZAN_SOIL_THRESHOLDS");
    }
    if (!path || !path[0]) {
        FILE *f = fopen("/etc/zan-kiosk-soil.json", "rb");
        if (f) {
            fclose(f);
            path = "/etc/zan-kiosk-soil.json";
        } else {
            path = NULL;
        }
    }
    if (path && path[0]) {
        read_file(cfg, path);
    }
    zk_soil_cfg_apply_env(cfg);
    return ZK_OK;
}

const char *zk_soil_word(const zk_soil_cfg_t *cfg, const char *station_id, int percent)
{
    int dry = 30;
    int wet = 80;
    int i;
    if (cfg) {
        dry = cfg->dry_lt;
        wet = cfg->wet_gt;
        if (station_id && station_id[0]) {
            for (i = 0; i < cfg->n_stations; i++) {
                if (strcmp(cfg->stations[i].id, station_id) == 0) {
                    dry = cfg->stations[i].dry_lt;
                    wet = cfg->stations[i].wet_gt;
                    break;
                }
            }
        }
    }
    if (percent < dry) {
        return "Dry";
    }
    if (percent > wet) {
        return "Wet";
    }
    return "OK";
}
