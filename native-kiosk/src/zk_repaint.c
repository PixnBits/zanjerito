#include "zk_repaint.h"

#include <stdio.h>
#include <string.h>

static const char *k_names[] = {
    "boot",
    "return_online",
    "close_full_sheet",
    "minute_tick",
    "next_station",
    "idle_water",
    "pause_resume",
    "overlap",
    "sheet",
    "stepper",
    "open_stop_all",
    "controller_lost",
    "offline_confirm",
    "soil_rain",
    "manual",
    "retry_tick",
    "ack",
    "schedules"
};

const char *zk_repaint_name(zk_repaint_event_t ev)
{
    if ((int)ev < 0 || (int)ev >= (int)(sizeof k_names / sizeof k_names[0])) {
        return "unknown";
    }
    return k_names[ev];
}

static int push(zk_rect *out, int n, int max, zk_rect r)
{
    if (!out || n >= max || r.w <= 0 || r.h <= 0) {
        return n;
    }
    out[n] = r;
    return n + 1;
}

static zk_rect full(void)
{
    zk_rect r;
    r.x = 0;
    r.y = 0;
    r.w = ZK_SCREEN_W;
    r.h = ZK_SCREEN_H;
    return r;
}

static zk_rect hero(void)
{
    zk_rect r;
    r.x = ZK_HERO_X;
    r.y = ZK_HERO_Y;
    r.w = ZK_HERO_W;
    r.h = ZK_HERO_H;
    return r;
}

static zk_rect pause_r(void)
{
    zk_rect r;
    r.x = ZK_PAUSE_X;
    r.y = ZK_BAR_Y;
    r.w = ZK_PAUSE_W;
    r.h = ZK_PAUSE_H;
    return r;
}

static zk_rect sched_r(void)
{
    zk_rect r;
    r.x = ZK_SCHED_X;
    r.y = ZK_BAR_Y;
    r.w = ZK_SCHED_W;
    r.h = ZK_SCHED_H;
    return r;
}

static zk_rect stop_r(void)
{
    zk_rect r;
    r.x = ZK_STOP_X;
    r.y = ZK_BAR_Y;
    r.w = ZK_STOP_W;
    r.h = ZK_STOP_H;
    return r;
}

static zk_rect tile_r(int n_stations, int index)
{
    zk_layout_in in;
    zk_layout_rects L;
    memset(&in, 0, sizeof in);
    in.n_stations = n_stations;
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &L);
    if (index < 0 || index >= L.n_tiles) {
        zk_rect z;
        z.x = z.y = z.w = z.h = 0;
        return z;
    }
    return L.tiles[index];
}

int zk_repaint_rects(zk_repaint_event_t ev, const zk_repaint_ctx_t *ctx, zk_rect *out, int max)
{
    zk_repaint_ctx_t dummy;
    int n = 0;
    int i;
    int shown;
    if (!ctx) {
        memset(&dummy, 0, sizeof dummy);
        dummy.tile_a = -1;
        dummy.tile_b = -1;
        ctx = &dummy;
    }
    shown = zk_layout_shown(ctx->n_stations);
    switch (ev) {
    case ZK_EVT_BOOT:
    case ZK_EVT_RETURN_ONLINE:
    case ZK_EVT_CLOSE_FULL_SHEET:
    case ZK_EVT_OPEN_STOP_ALL:
    case ZK_EVT_OFFLINE_CONFIRM:
        return push(out, 0, max, full());
    case ZK_EVT_MINUTE_TICK: {
        zk_rect t;
        t.x = 40;
        t.y = 215;
        t.w = 328;
        t.h = 77;
        n = push(out, n, max, t);
        n = push(out, n, max, tile_r(ctx->n_stations, ctx->tile_a));
        return n;
    }
    case ZK_EVT_NEXT_STATION:
        n = push(out, n, max, hero());
        n = push(out, n, max, tile_r(ctx->n_stations, ctx->tile_a));
        n = push(out, n, max, tile_r(ctx->n_stations, ctx->tile_b));
        return n;
    case ZK_EVT_IDLE_WATER:
    case ZK_EVT_MANUAL:
        n = push(out, n, max, hero());
        n = push(out, n, max, tile_r(ctx->n_stations, ctx->tile_a));
        n = push(out, n, max, stop_r());
        return n;
    case ZK_EVT_PAUSE_RESUME:
        n = push(out, n, max, hero());
        n = push(out, n, max, pause_r());
        return n;
    case ZK_EVT_OVERLAP:
        return push(out, 0, max, hero());
    case ZK_EVT_SHEET: {
        zk_rect s;
        s.x = ZK_SHEET_X;
        s.y = ZK_SHEET_Y;
        s.w = ZK_SHEET_W;
        s.h = ZK_SHEET_H;
        return push(out, 0, max, s);
    }
    case ZK_EVT_STEPPER:
        {
            zk_rect v;
            zk_rect title;
            v.x = 144;
            v.y = 232;
            v.w = 132;
            v.h = 96;
            title.x = 36;
            title.y = 62;
            title.w = 728;
            title.h = 80;
            n = push(out, n, max, v);
            n = push(out, n, max, title);
            if (ctx->stepper_cross) {
                zk_rect m;
                m.x = 36;
                m.y = 232;
                m.w = 96;
                m.h = 96;
                n = push(out, n, max, m);
            }
        }
        return n;
    case ZK_EVT_CONTROLLER_LOST:
        n = push(out, n, max, hero());
        for (i = 0; i < shown; i++) {
            n = push(out, n, max, tile_r(ctx->n_stations, i));
        }
        n = push(out, n, max, pause_r());
        n = push(out, n, max, sched_r());
        return n;
    case ZK_EVT_SOIL_RAIN:
        for (i = 0; i < shown; i++) {
            zk_rect slot;
            if ((ctx->soil_mask & (1u << i)) == 0) {
                continue;
            }
            zk_layout_slot(ctx->n_stations, i, &slot);
            n = push(out, n, max, slot);
        }
        if (ctx->rain) {
            zk_rect rain;
            rain.x = ZK_RAIN_X;
            rain.y = ZK_RAIN_Y;
            rain.w = ZK_RAIN_W;
            rain.h = ZK_RAIN_H;
            n = push(out, n, max, rain);
        }
        return n;
    case ZK_EVT_RETRY_TICK: {
        zk_rect lab;
        lab.x = ZK_LABEL_X;
        lab.y = ZK_LABEL_Y;
        lab.w = ZK_LABEL_W;
        lab.h = ZK_LABEL_H;
        return push(out, 0, max, lab);
    }
    case ZK_EVT_ACK:
        n = push(out, n, max, hero());
        for (i = 0; i < shown; i++) {
            n = push(out, n, max, tile_r(ctx->n_stations, i));
        }
        n = push(out, n, max, pause_r());
        n = push(out, n, max, stop_r());
        return n;
    case ZK_EVT_SCHEDULES: {
        zk_rect s;
        s.x = ZK_SHEET_X;
        s.y = ZK_SHEET_Y;
        s.w = ZK_SHEET_W;
        s.h = ZK_SHEET_H;
        n = push(out, n, max, s);
        n = push(out, n, max, sched_r());
        return n;
    }
    default:
        return 0;
    }
}

int zk_repaint_format(zk_repaint_event_t ev, const zk_repaint_ctx_t *ctx, char *dst, size_t cap)
{
    zk_rect rects[16];
    int n;
    int i;
    int used;
    if (!dst || cap == 0) {
        return -1;
    }
    dst[0] = 0;
    n = zk_repaint_rects(ev, ctx, rects, 16);
    used = snprintf(dst, cap, "dirty event=%s rects=", zk_repaint_name(ev));
    if (used < 0 || (size_t)used >= cap) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        int w = snprintf(dst + used, cap - (size_t)used, "%s%d,%d,%dx%d", i ? ";" : "", rects[i].x, rects[i].y,
                         rects[i].w, rects[i].h);
        if (w < 0 || (size_t)w >= cap - (size_t)used) {
            return -1;
        }
        used += w;
    }
    if ((size_t)used + 2 > cap) {
        return -1;
    }
    dst[used++] = '\n';
    dst[used] = 0;
    return used;
}

void zk_repaint_log(zk_repaint_event_t ev, const zk_repaint_ctx_t *ctx)
{
    char line[512];
    if (zk_repaint_format(ev, ctx, line, sizeof line) > 0) {
        fputs(line, stderr);
    }
}
