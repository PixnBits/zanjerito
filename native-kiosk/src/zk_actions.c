#include "zk_actions.h"

#include "zk_http.h"
#include "zk_model.h"

#include <stdio.h>
#include <string.h>

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
