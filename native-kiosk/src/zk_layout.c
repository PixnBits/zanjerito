#include "zk_layout.h"

#include <string.h>

#define PAD_TOP     12
#define PAD_SIDE    14
#define PAD_BOT     20
#define MAIN_RAIL_GAP 14
#define RAIL_W      236
#define RAIL_X      550
#define MAIN_X      14
#define MAIN_Y      12
#define MAIN_W      522
#define MAIN_H      448 /* y 12..460 */
#define MAIN_BOTTOM 460

#define STOP_H      264
#define STOP_H_RUN  304
#define SLOT_GAP    16
#define SLOT_H      168
#define SLOT_H_RUN  128

#define HDR_H       108
#define RAIN_H      58
#define GAP         10
#define TILE_GAP    10
#define BANNER_H    118
#define TITLE_H     50
#define CHIP_GAP    12
#define HOLD_H      114
#define SCHED_HDR_H 72
#define SCHED_ROW_H 50
#define STEP_STRIP_H 6
#define SCHED_BTN_W 120
#define SCHED_BTN_H 108

#define MODAL_W     620
#define MODAL_H     300
#define MODAL_X     90
#define MODAL_Y     90
#define BTN_W       270
#define BTN_H       130
#define BTN_GAP     20

static int clampi(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static zk_rect R(int x, int y, int w, int h)
{
    zk_rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static int rect_empty(zk_rect r)
{
    return r.w <= 0 || r.h <= 0;
}

static void add_tgt(zk_target *out, int max, int *n, enum zk_target_id id, zk_rect r, enum zk_target_kind kind)
{
    if (rect_empty(r)) {
        return;
    }
    if (*n < max && out) {
        out[*n].id = id;
        out[*n].rect = r;
        out[*n].kind = kind;
    }
    (*n)++;
}

static void rail_geometry(enum zk_screen screen, zk_rect *stop, zk_rect *slot)
{
    int stop_h = (screen == ZK_SCREEN_RUNNING) ? STOP_H_RUN : STOP_H;
    int slot_h = (screen == ZK_SCREEN_RUNNING) ? SLOT_H_RUN : SLOT_H;
    int slot_y = PAD_TOP + stop_h + SLOT_GAP;
    *stop = R(RAIL_X, PAD_TOP, RAIL_W, stop_h);
    if (screen == ZK_SCREEN_NEEDS_UPDATE) {
        *slot = R(0, 0, 0, 0);
    } else {
        *slot = R(RAIL_X, slot_y, RAIL_W, slot_h);
    }
}

static zk_rect schedules_btn(void)
{
    return R(MAIN_X + MAIN_W - SCHED_BTN_W, MAIN_Y, SCHED_BTN_W, SCHED_BTN_H);
}

static void home_tiles(const zk_layout_in *in, zk_layout_rects *out)
{
    int rain = in && in->rain_visible;
    int nst = in ? clampi(in->n_stations, 0, 8) : 0;
    int y0 = MAIN_Y + HDR_H + GAP;
    int avail;
    int tile_h, tile_w;
    int n_show, n_rows, cols;
    int i;
    if (rain) {
        y0 += RAIN_H + GAP;
    }
    avail = MAIN_BOTTOM - y0;
    /* 2-row cell height matches the mockup (126 with rain, 160 without). */
    tile_h = (avail - TILE_GAP) / 2;
    if (tile_h < 1) {
        tile_h = 1;
    }
    n_show = nst;
    n_rows = 2;
    cols = 2;
    if (n_show <= 2) {
        n_rows = 1;
    }
    if (n_show == 1) {
        cols = 1;
        tile_w = MAIN_W;
    } else if (n_show <= 4) {
        cols = 2;
        tile_w = (MAIN_W - TILE_GAP) / 2;
    } else if (n_show <= 6) {
        cols = 3;
        n_rows = 2;
        tile_w = (MAIN_W - 2 * TILE_GAP) / 3;
    } else {
        cols = 4;
        n_rows = 2;
        tile_w = (MAIN_W - 3 * TILE_GAP) / 4;
    }
    if (tile_w < 1) {
        tile_w = 1;
    }
    out->n_tiles = 0;
    for (i = 0; i < n_show && i < 8; i++) {
        int row = (cols == 1) ? 0 : (i / cols);
        int col = (cols == 1) ? 0 : (i % cols);
        if (n_rows == 1) {
            row = 0;
        }
        out->tiles[i] = R(MAIN_X + col * (tile_w + TILE_GAP),
                          y0 + row * (tile_h + TILE_GAP),
                          tile_w, tile_h);
        out->n_tiles++;
    }
}

void zk_layout_rects_of(enum zk_screen screen, const zk_layout_in *in, zk_layout_rects *out)
{
    int rain = in && in->rain_visible;
    int n_sched;
    int i;
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->screen = R(0, 0, ZK_SCREEN_W, ZK_SCREEN_H);
    out->main = R(MAIN_X, MAIN_Y, MAIN_W, MAIN_H);
    out->rail = R(RAIL_X, PAD_TOP, RAIL_W, MAIN_H);
    out->step_strip = R(0, ZK_SCREEN_H - STEP_STRIP_H, ZK_SCREEN_W, STEP_STRIP_H);
    rail_geometry(screen, &out->stop, &out->slot);

    if (screen == ZK_SCREEN_CONFIRM_STOP || screen == ZK_SCREEN_CONFIRM_PAUSE) {
        int inner = MODAL_W - 2 * BTN_W - BTN_GAP;
        int side = inner / 2;
        int by = MODAL_Y + MODAL_H - 20 - BTN_H;
        out->modal = R(MODAL_X, MODAL_Y, MODAL_W, MODAL_H);
        out->modal_ok = R(MODAL_X + side, by, BTN_W, BTN_H);
        out->modal_cancel = R(MODAL_X + side + BTN_W + BTN_GAP, by, BTN_W, BTN_H);
        return;
    }

    switch (screen) {
    case ZK_SCREEN_HOME:
        out->schedules = schedules_btn();
        out->header = R(MAIN_X, MAIN_Y, MAIN_W - SCHED_BTN_W - GAP, HDR_H);
        if (rain) {
            out->rain = R(MAIN_X, MAIN_Y + HDR_H + GAP, MAIN_W, RAIN_H);
        }
        home_tiles(in, out);
        break;
    case ZK_SCREEN_RUNNING:
        out->header = R(MAIN_X, MAIN_Y, MAIN_W, HDR_H);
        break;
    case ZK_SCREEN_PAUSED: {
        int y = MAIN_Y;
        int info_h;
        int left_w = MAIN_W - SCHED_BTN_W - GAP;
        out->schedules = schedules_btn();
        out->banner = R(MAIN_X, y, left_w, BANNER_H);
        y += BANNER_H + GAP;
        if (rain) {
            out->rain = R(MAIN_X, y, MAIN_W, RAIN_H);
            y += RAIN_H + GAP;
        }
        info_h = MAIN_BOTTOM - y;
        if (info_h < ZK_MIN_TAP) {
            info_h = ZK_MIN_TAP;
        }
        /* STOP must remain the tallest target on non-modal screens. */
        if (info_h > STOP_H) {
            info_h = STOP_H;
        }
        out->info = R(MAIN_X, y, MAIN_W, info_h);
        break;
    }
    case ZK_SCREEN_PICKER: {
        int y = MAIN_Y + TITLE_H + GAP;
        int avail = MAIN_BOTTOM - y;
        int ch = (avail - CHIP_GAP) / 2;
        int cw = (MAIN_W - CHIP_GAP) / 2;
        out->title = R(MAIN_X, MAIN_Y, MAIN_W, TITLE_H);
        out->chips[0] = R(MAIN_X, y, cw, ch);
        out->chips[1] = R(MAIN_X + cw + CHIP_GAP, y, cw, ch);
        out->chips[2] = R(MAIN_X, y + ch + CHIP_GAP, cw, ch);
        out->chips[3] = R(MAIN_X + cw + CHIP_GAP, y + ch + CHIP_GAP, cw, ch);
        break;
    }
    case ZK_SCREEN_SCHEDULES: {
        int y = MAIN_Y + TITLE_H + GAP;
        int card_h = MAIN_BOTTOM - y;
        int row_space;
        out->title = R(MAIN_X, MAIN_Y, MAIN_W, TITLE_H);
        out->card = R(MAIN_X, y, MAIN_W, card_h);
        out->sched_header = R(MAIN_X, y, MAIN_W, SCHED_HDR_H);
        out->hold_edit = R(MAIN_X, y + card_h - HOLD_H, MAIN_W, HOLD_H);
        row_space = card_h - SCHED_HDR_H - HOLD_H;
        if (row_space < 0) {
            row_space = 0;
        }
        n_sched = in ? in->n_schedules : 0;
        if (n_sched < 0) {
            n_sched = 0;
        }
        out->n_sched_rows = row_space / SCHED_ROW_H;
        if (out->n_sched_rows > 5) {
            out->n_sched_rows = 5;
        }
        if (n_sched > 0 && out->n_sched_rows > n_sched) {
            out->n_sched_rows = n_sched;
        }
        for (i = 0; i < out->n_sched_rows; i++) {
            out->sched_rows[i] = R(MAIN_X, y + SCHED_HDR_H + i * SCHED_ROW_H, MAIN_W, SCHED_ROW_H);
        }
        break;
    }
    case ZK_SCREEN_STATION:
        out->title = R(MAIN_X, MAIN_Y, MAIN_W, TITLE_H);
        out->card = R(MAIN_X, MAIN_Y + TITLE_H + GAP, MAIN_W, MAIN_BOTTOM - (MAIN_Y + TITLE_H + GAP));
        break;
    case ZK_SCREEN_NEEDS_UPDATE:
        out->card = R(MAIN_X, MAIN_Y, MAIN_W, MAIN_H);
        break;
    default:
        break;
    }
}

int zk_layout_targets(enum zk_screen screen, const zk_layout_in *in, zk_target out[], int max)
{
    zk_layout_rects L;
    int n = 0;
    int i;
    if (max < 0) {
        max = 0;
    }
    zk_layout_rects_of(screen, in, &L);

    if (screen == ZK_SCREEN_CONFIRM_STOP || screen == ZK_SCREEN_CONFIRM_PAUSE) {
        add_tgt(out, max, &n, ZK_TARGET_MODAL_OK, L.modal_ok, ZK_KIND_MODAL);
        add_tgt(out, max, &n, ZK_TARGET_MODAL_CANCEL, L.modal_cancel, ZK_KIND_MODAL);
        return n;
    }

    add_tgt(out, max, &n, ZK_TARGET_STOP, L.stop, ZK_KIND_BUTTON);

    switch (screen) {
    case ZK_SCREEN_HOME:
        add_tgt(out, max, &n, ZK_TARGET_PAUSE, L.slot, ZK_KIND_BUTTON);
        add_tgt(out, max, &n, ZK_TARGET_HEADER_NEXT, L.header, ZK_KIND_HEADER);
        add_tgt(out, max, &n, ZK_TARGET_SCHEDULES, L.schedules, ZK_KIND_BUTTON);
        for (i = 0; i < L.n_tiles; i++) {
            add_tgt(out, max, &n, (enum zk_target_id)(ZK_TARGET_TILE0 + i), L.tiles[i], ZK_KIND_TILE);
        }
        break;
    case ZK_SCREEN_RUNNING:
        add_tgt(out, max, &n, ZK_TARGET_PAUSE, L.slot, ZK_KIND_BUTTON);
        break;
    case ZK_SCREEN_PAUSED:
        add_tgt(out, max, &n, ZK_TARGET_RESUME, L.slot, ZK_KIND_BUTTON);
        add_tgt(out, max, &n, ZK_TARGET_INFO_CARD, L.info, ZK_KIND_CARD);
        add_tgt(out, max, &n, ZK_TARGET_SCHEDULES, L.schedules, ZK_KIND_BUTTON);
        break;
    case ZK_SCREEN_PICKER:
        add_tgt(out, max, &n, ZK_TARGET_BACK, L.slot, ZK_KIND_BUTTON);
        add_tgt(out, max, &n, ZK_TARGET_CHIP0, L.chips[0], ZK_KIND_CHIP);
        add_tgt(out, max, &n, ZK_TARGET_CHIP1, L.chips[1], ZK_KIND_CHIP);
        add_tgt(out, max, &n, ZK_TARGET_CHIP2, L.chips[2], ZK_KIND_CHIP);
        add_tgt(out, max, &n, ZK_TARGET_CHIP3, L.chips[3], ZK_KIND_CHIP);
        break;
    case ZK_SCREEN_SCHEDULES:
        add_tgt(out, max, &n, ZK_TARGET_BACK, L.slot, ZK_KIND_BUTTON);
        add_tgt(out, max, &n, ZK_TARGET_HOLD_EDIT, L.hold_edit, ZK_KIND_BUTTON);
        break;
    case ZK_SCREEN_STATION:
        add_tgt(out, max, &n, ZK_TARGET_BACK, L.slot, ZK_KIND_BUTTON);
        break;
    case ZK_SCREEN_NEEDS_UPDATE:
        break;
    default:
        break;
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
