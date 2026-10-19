#ifndef ZK_REPAINT_H
#define ZK_REPAINT_H

#include "zk_layout.h"

#include <stddef.h>

typedef enum {
    ZK_EVT_BOOT = 0,
    ZK_EVT_RETURN_ONLINE,
    ZK_EVT_CLOSE_FULL_SHEET,
    ZK_EVT_MINUTE_TICK,
    ZK_EVT_NEXT_STATION,
    ZK_EVT_IDLE_WATER,
    ZK_EVT_PAUSE_RESUME,
    ZK_EVT_OVERLAP,
    ZK_EVT_SHEET,
    ZK_EVT_STEPPER,
    ZK_EVT_OPEN_STOP_ALL,
    ZK_EVT_CONTROLLER_LOST,
    ZK_EVT_OFFLINE_CONFIRM,
    ZK_EVT_SOIL_RAIN,
    ZK_EVT_MANUAL,
    ZK_EVT_RETRY_TICK,
    ZK_EVT_ACK,
    ZK_EVT_SCHEDULES
} zk_repaint_event_t;

typedef struct {
    int n_stations;
    int tile_a;       /* running, affected, old, or manual tile; -1 if none */
    int tile_b;       /* new tile for next-station; -1 if none */
    unsigned soil_mask;
    int rain;         /* include the rain rect */
    int stepper_cross; /* include the minus button */
} zk_repaint_ctx_t;

const char *zk_repaint_name(zk_repaint_event_t ev);
int zk_repaint_rects(zk_repaint_event_t ev, const zk_repaint_ctx_t *ctx, zk_rect *out, int max);
/* "dirty event=<name> rects=x,y,wxh;..." including the newline. */
int zk_repaint_format(zk_repaint_event_t ev, const zk_repaint_ctx_t *ctx, char *dst, size_t cap);
void zk_repaint_log(zk_repaint_event_t ev, const zk_repaint_ctx_t *ctx);

#endif
