#ifndef ZK_ACTIONS_H
#define ZK_ACTIONS_H

typedef struct {
    int allow_writes;
    char base[128];
} zk_actions_t;

int zk_action_stop(const zk_actions_t *a, int *http_status);
int zk_action_pause(const zk_actions_t *a, const char *body, int *http_status);
int zk_action_resume(const zk_actions_t *a, int *http_status);

#endif
