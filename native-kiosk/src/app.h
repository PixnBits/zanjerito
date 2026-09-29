#ifndef ZK_APP_H
#define ZK_APP_H

/* Parsed run options. Data snapshot hooks stay out of this struct until
 * the shot/data layer needs them. */
typedef struct {
    const char *api;
    const char *fixture_dir;
    int allow_writes;
    int live_clock;
} zk_app_t;

#endif
