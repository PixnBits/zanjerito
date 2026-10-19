#ifndef ZK_ACTIONS_H
#define ZK_ACTIONS_H

#include <stddef.h>

typedef struct {
    int allow_writes;
    char base[128];
} zk_actions_t;

int zk_action_stop(const zk_actions_t *a, int *http_status);
int zk_action_pause(const zk_actions_t *a, const char *body, int *http_status);
int zk_action_resume(const zk_actions_t *a, int *http_status);
/* body is "<station-id>\\n{durationSec JSON}". The id is not sent in the JSON. */
int zk_action_run(const zk_actions_t *a, const char *body, int *http_status);
/* Writes the queue body. duration_sec is seconds. preempt is 0 or 1. */
int zk_action_run_body(char *dst, size_t cap, const char *station_id, int duration_sec, int preempt);

#endif
