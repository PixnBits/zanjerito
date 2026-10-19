#include "zk_actions.h"

#include "zk_http.h"
#include "zk_model.h"

#include <stdio.h>
#include <string.h>

static int station_id_ok(const char *id)
{
    size_t i;
    if (!id || !id[0]) {
        return 0;
    }
    for (i = 0; id[i]; i++) {
        char c = id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '-')) {
            return 0;
        }
        if (i >= 31) {
            return 0;
        }
    }
    return 1;
}

int zk_action_run_body(char *dst, size_t cap, const char *station_id, int duration_sec, int preempt)
{
    int n;
    if (!dst || cap == 0 || !station_id_ok(station_id) || duration_sec <= 0) {
        return ZK_ERR_ARG;
    }
    if (preempt) {
        n = snprintf(dst, cap, "%s\n{\"durationSec\":%d,\"preempt\":true}", station_id, duration_sec);
    } else {
        n = snprintf(dst, cap, "%s\n{\"durationSec\":%d}", station_id, duration_sec);
    }
    if (n < 0 || (size_t)n >= cap) {
        return ZK_ERR_OVERFLOW;
    }
    return ZK_OK;
}

static int post_or_ro(const zk_actions_t *a, const char *path, const char *body, int *http_status)
{
    char resp[4096];
    int st = 0;
    int rc;
    if (http_status) {
        *http_status = 0;
    }
    if (!a || !path) {
        return ZK_ERR_ARG;
    }
    if (!body) {
        body = "";
    }
    if (!a->allow_writes) {
        fprintf(stderr, "read-only: would POST %s %s\n", path, body);
        return ZK_RO;
    }
    if (!a->base[0]) {
        return ZK_ERR_ARG;
    }
    rc = zk_http_post(a->base, path, body, 5000, resp, sizeof(resp), &st);
    if (http_status) {
        *http_status = st;
    }
    if (rc != ZK_OK) {
        return rc;
    }
    if (st < 200 || st >= 300) {
        return ZK_ERR_HTTP;
    }
    return ZK_OK;
}

int zk_action_stop(const zk_actions_t *a, int *http_status)
{
    return post_or_ro(a, "/api/run/cancel", "", http_status);
}

int zk_action_pause(const zk_actions_t *a, const char *body, int *http_status)
{
    return post_or_ro(a, "/api/pause", body, http_status);
}

int zk_action_resume(const zk_actions_t *a, int *http_status)
{
    return post_or_ro(a, "/api/pause/resume", "", http_status);
}

int zk_action_run(const zk_actions_t *a, const char *body, int *http_status)
{
    char id[40];
    char path[96];
    const char *nl;
    const char *json;
    size_t n;
    if (!body) {
        return ZK_ERR_ARG;
    }
    nl = strchr(body, '\n');
    if (!nl || nl == body) {
        return ZK_ERR_ARG;
    }
    n = (size_t)(nl - body);
    if (n >= sizeof id) {
        return ZK_ERR_OVERFLOW;
    }
    memcpy(id, body, n);
    id[n] = 0;
    if (!station_id_ok(id)) {
        return ZK_ERR_ARG;
    }
    json = nl + 1;
    if (snprintf(path, sizeof path, "/api/stations/%s/run", id) >= (int)sizeof path) {
        return ZK_ERR_OVERFLOW;
    }
    return post_or_ro(a, path, json, http_status);
}
