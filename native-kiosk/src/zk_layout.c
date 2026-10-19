#include "zk_layout.h"

#include <string.h>

static zk_rect R(int x, int y, int w, int h)
{
    zk_rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static int sheet_of(enum zk_screen screen)
{
    switch (screen) {
    case ZK_SCREEN_SHEET_RUN:
        return ZK_SHEET_RUN;
    case ZK_SCREEN_SHEET_ANYWAY:
        return ZK_SHEET_ANYWAY;
    case ZK_SCREEN_SHEET_STOP_STATION:
        return ZK_SHEET_STOP_STATION;
    case ZK_SCREEN_SHEET_STOP_ALL:
        return ZK_SHEET_STOP_ALL;
    case ZK_SCREEN_SHEET_OVERLAP:
        return ZK_SHEET_OVERLAP;
    default:
        return ZK_SHEET_NONE;
    }
}

int zk_layout_shown(int n_stations)
{
    if (n_stations < 0) {
        return 0;
    }
    if (n_stations > ZK_MAX_SHOWN) {
        return ZK_MAX_SHOWN;
    }
    return n_stations;
}

static int rows_for(int n)
{
    return n > 4 ? 3 : 2;
}

void zk_layout_slot(int n_stations, int index, zk_rect *out)
{
    int n;
    int col;
    int row;
    int x;
    int y;
    if (!out) {
        return;
    }
    memset(out, 0, sizeof *out);
    n = zk_layout_shown(n_stations);
    if (index < 0 || index >= n) {
        return;
    }
    col = index % 2;
    row = index / 2;
    x = (col ? ZK_TILE_X1 : ZK_TILE_X0) + ZK_SLOT_INSET;
    if (n > 4) {
        y = ZK_SLOT_Y6 + row * ZK_SLOT_STRIDE6;
    } else {
        y = (row ? 188 : ZK_GRID_Y) + ZK_SLOT_DY4;
    }
    *out = R(x, y, ZK_SLOT_W, ZK_SLOT_H);
}

static void place_tiles(int n, zk_layout_rects *out)
{
    int i;
    int rows;
    int h;
    int y0[3];
    out->n_tiles = n;
    rows = rows_for(n);
    h = rows == 3 ? ZK_TILE_H6 : ZK_TILE_H4;
    out->tile_h = n > 0 ? h : 0;
    y0[0] = ZK_GRID_Y;
    y0[1] = rows == 3 ? 130 : 188;
    y0[2] = 244;
    for (i = 0; i < ZK_MAX_SHOWN; i++) {
        out->tiles[i] = R(0, 0, 0, 0);
        out->slots[i] = R(0, 0, 0, 0);
    }
    for (i = 0; i < n; i++) {
        int col = i % 2;
        int row = i / 2;
        out->tiles[i] = R(col ? ZK_TILE_X1 : ZK_TILE_X0, y0[row], ZK_TILE_W, h);
        zk_layout_slot(n > 4 ? 6 : 4, i, &out->slots[i]);
    }
}

void zk_layout_rects_of(enum zk_screen screen, const zk_layout_in *in, zk_layout_rects *out)
{
    int n;
    int sheet;
    zk_layout_in dummy;
    if (!out) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (!in) {
        memset(&dummy, 0, sizeof dummy);
        in = &dummy;
    }
    n = zk_layout_shown(in->n_stations);
    sheet = sheet_of(screen);
    out->screen = R(0, 0, ZK_SCREEN_W, ZK_SCREEN_H);
    out->hero = R(ZK_HERO_X, ZK_HERO_Y, ZK_HERO_W, ZK_HERO_H);
    out->pause = R(ZK_PAUSE_X, ZK_BAR_Y, ZK_PAUSE_W, ZK_PAUSE_H);
    out->stop = R(ZK_STOP_X, ZK_BAR_Y, ZK_STOP_W, ZK_STOP_H);
    out->rain = R(ZK_RAIN_X, ZK_RAIN_Y, ZK_RAIN_W, ZK_RAIN_H);
    out->label = R(ZK_LABEL_X, ZK_LABEL_Y, ZK_LABEL_W, ZK_LABEL_H);
    place_tiles(n, out);
    if (sheet == ZK_SHEET_STOP_ALL) {
        out->sheet = R(ZK_SHEET_X, ZK_SHEET_Y, ZK_SHEET_W, ZK_FULL_H);
        out->cancel = R(40, 328, 352, 112);
        out->stop_all = R(408, 328, 352, 112);
        out->sheet_title = R(36, 62, 728, 120);
    } else if (sheet != ZK_SHEET_NONE) {
        out->sheet = R(ZK_SHEET_X, ZK_SHEET_Y, ZK_SHEET_W, ZK_SHEET_H);
        out->sheet_title = R(36, 62, 728, 80);
        if (sheet == ZK_SHEET_RUN || sheet == ZK_SHEET_ANYWAY) {
            out->minus = R(36, 232, 96, 96);
            out->value = R(144, 232, 132, 96);
            out->plus = R(288, 232, 96, 96);
            out->cancel = R(408, 232, 168, 96);
            out->run = R(600, 232, 176, 96);
        } else if (sheet == ZK_SHEET_STOP_STATION) {
            out->cancel = R(36, 232, 352, 96);
            out->stop_station = R(400, 232, 364, 96);
        } else if (sheet == ZK_SHEET_OVERLAP) {
            out->ok = R(524, 232, 240, 96);
        }
    }
}

static int push(zk_target *out, int n, int max, enum zk_target_id id, zk_rect r, enum zk_target_kind kind)
{
    if (n >= max || r.w <= 0 || r.h <= 0) {
        return n;
    }
    out[n].id = id;
    out[n].rect = r;
    out[n].kind = kind;
    return n + 1;
}

int zk_layout_targets(enum zk_screen screen, const zk_layout_in *in, zk_target out[], int max)
{
    zk_layout_rects L;
    zk_layout_in dummy;
    int sheet;
    int n = 0;
    int i;
    if (!out || max <= 0) {
        return 0;
    }
    if (!in) {
        memset(&dummy, 0, sizeof dummy);
        in = &dummy;
    }
    zk_layout_rects_of(screen, in, &L);
    sheet = sheet_of(screen);
    if (sheet == ZK_SHEET_RUN || sheet == ZK_SHEET_ANYWAY) {
        if (in->minutes > 1) {
            n = push(out, n, max, ZK_TARGET_MINUS, L.minus, ZK_KIND_SHEET);
        }
        n = push(out, n, max, ZK_TARGET_PLUS, L.plus, ZK_KIND_SHEET);
        n = push(out, n, max, ZK_TARGET_CANCEL, L.cancel, ZK_KIND_SHEET);
        n = push(out, n, max, ZK_TARGET_RUN, L.run, ZK_KIND_SHEET);
    } else if (sheet == ZK_SHEET_STOP_STATION) {
        n = push(out, n, max, ZK_TARGET_CANCEL, L.cancel, ZK_KIND_SHEET);
        n = push(out, n, max, ZK_TARGET_STOP_STATION, L.stop_station, ZK_KIND_SHEET);
    } else if (sheet == ZK_SHEET_STOP_ALL) {
        n = push(out, n, max, ZK_TARGET_CANCEL, L.cancel, ZK_KIND_SHEET);
        n = push(out, n, max, ZK_TARGET_STOP_ALL, L.stop_all, ZK_KIND_SHEET);
        return n;
    } else if (sheet == ZK_SHEET_OVERLAP) {
        n = push(out, n, max, ZK_TARGET_OK, L.ok, ZK_KIND_SHEET);
    }
    /* Partial sheet: STOP stays live. Resume stays live while paused.
     * Pause and tiles are covered or not live. Full sheet returned above. */
    n = push(out, n, max, ZK_TARGET_STOP, L.stop, ZK_KIND_BUTTON);
    if (sheet == ZK_SHEET_NONE && !in->offline && in->overlap) {
        n = push(out, n, max, ZK_TARGET_HERO, L.hero, ZK_KIND_HERO);
    }
    if (sheet == ZK_SHEET_NONE && !in->offline) {
        for (i = 0; i < L.n_tiles; i++) {
            n = push(out, n, max, (enum zk_target_id)(ZK_TARGET_TILE0 + i), L.tiles[i], ZK_KIND_TILE);
        }
        if (in->paused) {
            n = push(out, n, max, ZK_TARGET_RESUME, L.pause, ZK_KIND_BUTTON);
        } else {
            n = push(out, n, max, ZK_TARGET_PAUSE, L.pause, ZK_KIND_BUTTON);
        }
    } else if (sheet != ZK_SHEET_NONE && in->paused && !in->offline) {
        n = push(out, n, max, ZK_TARGET_RESUME, L.pause, ZK_KIND_BUTTON);
    }
    return n;
}

static int contains(zk_rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

enum zk_target_id zk_hit_test(const zk_target *targets, int n, int x, int y)
{
    int i;
    if (!targets || n <= 0) {
        return ZK_TARGET_NONE;
    }
    for (i = 0; i < n; i++) {
        if (contains(targets[i].rect, x, y)) {
            return targets[i].id;
        }
    }
    return ZK_TARGET_NONE;
}

int zk_layout_hit_is_stop(enum zk_screen screen, const zk_layout_in *in, int x, int y)
{
    zk_target tg[24];
    int n = zk_layout_targets(screen, in, tg, 24);
    if (n > 24) {
        n = 24;
    }
    return zk_hit_test(tg, n, x, y) == ZK_TARGET_STOP;
}
