#include "zk_test.h"

#include "zk_layout.h"
#include "zk_power.h"

#include <unistd.h>

static int overlap_strict(zk_rect a, zk_rect b)
{
    int ax2 = a.x + a.w;
    int ay2 = a.y + a.h;
    int bx2 = b.x + b.w;
    int by2 = b.y + b.h;
    return a.x < bx2 && b.x < ax2 && a.y < by2 && b.y < ay2;
}

/* Inflating each side by 6 px makes a gap under 12 px into an overlap.
 * Edges that touch (gap exactly 12) do not overlap. */
static int gap_ok(zk_rect a, zk_rect b)
{
    zk_rect A = a;
    zk_rect B = b;
    A.x -= 6;
    A.y -= 6;
    A.w += 12;
    A.h += 12;
    B.x -= 6;
    B.y -= 6;
    B.w += 12;
    B.h += 12;
    return !overlap_strict(A, B);
}

static int short_side(zk_rect r)
{
    return r.w < r.h ? r.w : r.h;
}

static const zk_target *find_id(const zk_target *t, int n, enum zk_target_id id)
{
    int i;
    for (i = 0; i < n; i++) {
        if (t[i].id == id) {
            return &t[i];
        }
    }
    return NULL;
}

static int has_id(const zk_target *t, int n, enum zk_target_id id)
{
    return find_id(t, n, id) != NULL;
}

static void check_targets(enum zk_screen sc, const zk_layout_in *in, const char *tag)
{
    zk_target t[32];
    int n;
    int i, j;
    n = zk_layout_targets(sc, in, t, 32);
    TCHECK(n >= 0 && n <= 32, "%s n=%d", tag, n);
    for (i = 0; i < n; i++) {
        zk_rect r = t[i].rect;
        TCHECK(r.w > 0 && r.h > 0, "%s id=%d empty", tag, (int)t[i].id);
        TCHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= ZK_SCREEN_W && r.y + r.h <= ZK_SCREEN_H,
               "%s id=%d outside %d,%d %dx%d", tag, (int)t[i].id, r.x, r.y, r.w, r.h);
        TCHECK(short_side(r) >= ZK_MIN_TAP, "%s id=%d short %d", tag, (int)t[i].id, short_side(r));
        if (sc == ZK_SCREEN_HOME || sc == ZK_SCREEN_SCHEDULES) {
            TCHECK(short_side(r) >= 96, "%s id=%d short %d < 96", tag, (int)t[i].id, short_side(r));
        }
        TEQ_I((int)zk_hit_test(t, n, r.x, r.y), (int)t[i].id);
        TEQ_I((int)zk_hit_test(t, n, r.x + r.w - 1, r.y + r.h - 1), (int)t[i].id);
        TEQ_I((int)zk_hit_test(t, n, r.x + r.w, r.y), (int)ZK_TARGET_NONE);
        TEQ_I((int)zk_hit_test(t, n, r.x, r.y + r.h), (int)ZK_TARGET_NONE);
        for (j = i + 1; j < n; j++) {
            TCHECK(!overlap_strict(r, t[j].rect), "%s overlap %d vs %d", tag, (int)t[i].id, (int)t[j].id);
            TCHECK(gap_ok(r, t[j].rect), "%s gap %d vs %d (%d,%d %dx%d) (%d,%d %dx%d)", tag, (int)t[i].id,
                   (int)t[j].id, r.x, r.y, r.w, r.h, t[j].rect.x, t[j].rect.y, t[j].rect.w, t[j].rect.h);
        }
    }
}

static void test_min_tap_constant(void)
{
    int px = (int)(9.0 / 25.4 * (double)ZK_PX_PER_INCH + 0.5);
    TEQ_I(ZK_MIN_TAP, 95);
    TEQ_I(px, 95);
    TEQ_I(ZK_GAP_MIN, 12);
}

static void test_every_screen(void)
{
    static const enum zk_screen screens[] = {
        ZK_SCREEN_HOME,           ZK_SCREEN_SHEET_RUN, ZK_SCREEN_SHEET_ANYWAY,
        ZK_SCREEN_SHEET_STOP_STATION, ZK_SCREEN_SHEET_STOP_ALL, ZK_SCREEN_SHEET_OVERLAP, ZK_SCREEN_SCHEDULES};
    static const int counts[] = {0, 1, 4, 5, 6, 8};
    int s, c, paused, offline, overlap, minutes;
    for (s = 0; s < (int)(sizeof screens / sizeof screens[0]); s++) {
        for (c = 0; c < (int)(sizeof counts / sizeof counts[0]); c++) {
            for (paused = 0; paused <= 1; paused++) {
                for (offline = 0; offline <= 1; offline++) {
                    for (overlap = 0; overlap <= 1; overlap++) {
                        for (minutes = 1; minutes <= 60; minutes += 59) {
                            zk_layout_in in;
                            char tag[96];
                            memset(&in, 0, sizeof in);
                            in.n_stations = counts[c];
                            in.paused = paused;
                            in.offline = offline;
                            in.overlap = overlap;
                            in.minutes = minutes;
                            in.sched_cached = paused;
                            in.n_schedules = minutes > 1 ? 3 : 1;
                            snprintf(tag, sizeof tag, "sc=%d n=%d p=%d off=%d ov=%d m=%d", (int)screens[s],
                                     counts[c], paused, offline, overlap, minutes);
                            check_targets(screens[s], &in, tag);
                        }
                    }
                }
            }
        }
    }
}

static void test_grids(void)
{
    zk_layout_in in;
    zk_layout_rects R;
    zk_target t[32];
    int n;
    int i;
    memset(&in, 0, sizeof in);

    in.n_stations = 1;
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &R);
    TEQ_I(R.n_tiles, 1);
    TEQ_I(R.tiles[0].x, 404);
    TEQ_I(R.tiles[0].y, 16);
    TEQ_I(R.tiles[0].w, 184);
    TEQ_I(R.tiles[0].h, 160);

    in.n_stations = 4;
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &R);
    TEQ_I(R.n_tiles, 4);
    TEQ_I(R.tile_h, 160);
    TEQ_I(R.tiles[0].x, 404);
    TEQ_I(R.tiles[1].x, 600);
    TEQ_I(R.tiles[0].y, 16);
    TEQ_I(R.tiles[2].y, 188);
    TEQ_I(R.tiles[0].w, 184);
    TEQ_I(R.tiles[0].h, 160);
    TEQ_I(R.tiles[1].x - (R.tiles[0].x + R.tiles[0].w), 12);
    TEQ_I(R.tiles[2].y - (R.tiles[0].y + R.tiles[0].h), 12);
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_TILE0) && has_id(t, n, ZK_TARGET_TILE3), "4 tiles");
    TCHECK(!has_id(t, n, ZK_TARGET_TILE4), "no 5th on 4");

    in.n_stations = 5;
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &R);
    TEQ_I(R.n_tiles, 5);
    TEQ_I(R.tile_h, 102);
    TEQ_I(R.tiles[0].y, 16);
    TEQ_I(R.tiles[2].y, 130);
    TEQ_I(R.tiles[4].y, 244);
    TEQ_I(R.tiles[4].x, 404);
    TEQ_I(R.tiles[4].h, 102);
    TEQ_I(R.tiles[5].w, 0);
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_TILE4), "5th tile");
    TCHECK(!has_id(t, n, ZK_TARGET_TILE5), "5 leaves the last cell empty");

    in.n_stations = 6;
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &R);
    TEQ_I(R.n_tiles, 6);
    TEQ_I(R.tiles[5].x, 600);
    TEQ_I(R.tiles[5].y, 244);
    TEQ_I(R.tiles[5].h, 102);
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_TILE5), "6th tile");

    in.n_stations = 8;
    TEQ_I(zk_layout_shown(8), 6);
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &R);
    TEQ_I(R.n_tiles, 6);

    /* Soil slot geometry is fixed. It does not depend on whether soil is shown. */
    in.n_stations = 4;
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &R);
    TEQ_I(R.slots[0].x, 418);
    TEQ_I(R.slots[0].y, 101);
    TEQ_I(R.slots[0].w, 156);
    TEQ_I(R.slots[0].h, 24);
    TEQ_I(R.slots[1].x, 614);
    TEQ_I(R.slots[2].y, 273);
    in.n_stations = 6;
    for (i = 0; i < 6; i++) {
        zk_rect slot;
        int col = i % 2;
        int row = i / 2;
        zk_layout_slot(6, i, &slot);
        TEQ_I(slot.x, (col ? 614 : 418));
        TEQ_I(slot.y, 69 + row * 114);
        TEQ_I(slot.w, 156);
        TEQ_I(slot.h, 24);
    }
}

static void test_bar_and_sheets(void)
{
    zk_layout_in in;
    zk_layout_rects R;
    zk_target t[32];
    int n;
    memset(&in, 0, sizeof in);
    in.n_stations = 4;
    in.minutes = 6;
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &R);
    TEQ_I(R.hero.x, 16);
    TEQ_I(R.hero.y, 16);
    TEQ_I(R.hero.w, 376);
    TEQ_I(R.hero.h, 332);
    TEQ_I(R.sched.x, 16);
    TEQ_I(R.sched.y, 360);
    TEQ_I(R.sched.w, 120);
    TEQ_I(R.sched.h, 104);
    TEQ_I(R.pause.x, 148);
    TEQ_I(R.pause.y, 360);
    TEQ_I(R.pause.w, 244);
    TEQ_I(R.pause.h, 104);
    TEQ_I(R.pause.x - (R.sched.x + R.sched.w), 12);
    TEQ_I(R.stop.x - (R.pause.x + R.pause.w), 12);
    TEQ_I(R.stop.x, 404);
    TEQ_I(R.stop.y, 360);
    TEQ_I(R.stop.w, 380);
    TEQ_I(R.stop.h, 104);
    TEQ_I(R.rain.x, 40);
    TEQ_I(R.rain.y, 264);
    TEQ_I(R.rain.w, 328);
    TEQ_I(R.rain.h, 64);

    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_PAUSE), "pause");
    TCHECK(has_id(t, n, ZK_TARGET_SCHEDULES), "schedules");
    TCHECK(has_id(t, n, ZK_TARGET_STOP), "stop");
    TEQ_I((int)zk_hit_test(t, n, 76, 412), (int)ZK_TARGET_SCHEDULES);
    TCHECK(!has_id(t, n, ZK_TARGET_HERO), "hero is not a target until overlap");
    in.overlap = 1;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_HERO), "overlap hero");
    in.overlap = 0;
    in.paused = 1;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_RESUME), "resume");
    TCHECK(!has_id(t, n, ZK_TARGET_PAUSE), "no pause while paused");

    in.paused = 0;
    in.offline = 1;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TEQ_I(n, 1);
    TCHECK(has_id(t, n, ZK_TARGET_STOP), "offline STOP");
    TCHECK(!has_id(t, n, ZK_TARGET_TILE0), "offline tiles are not targets");
    TCHECK(!has_id(t, n, ZK_TARGET_PAUSE), "offline pause is not a target");
    TCHECK(!has_id(t, n, ZK_TARGET_SCHEDULES), "offline schedules with no cache");
    in.sched_cached = 1;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_SCHEDULES), "offline schedules when cached");
    TCHECK(has_id(t, n, ZK_TARGET_STOP), "offline STOP stays");
    TCHECK(!has_id(t, n, ZK_TARGET_PAUSE), "offline pause stays disabled");
    in.sched_cached = 0;

    in.offline = 0;
    in.minutes = 1;
    n = zk_layout_targets(ZK_SCREEN_SHEET_RUN, &in, t, 32);
    TCHECK(!has_id(t, n, ZK_TARGET_MINUS), "minus disabled at 1");
    TCHECK(has_id(t, n, ZK_TARGET_PLUS) && has_id(t, n, ZK_TARGET_RUN) && has_id(t, n, ZK_TARGET_CANCEL),
           "run sheet buttons");
    TCHECK(has_id(t, n, ZK_TARGET_STOP), "STOP stays live");
    TCHECK(!has_id(t, n, ZK_TARGET_TILE0), "tiles covered");
    TCHECK(!has_id(t, n, ZK_TARGET_PAUSE), "pause not live under a sheet");
    in.paused = 1;
    n = zk_layout_targets(ZK_SCREEN_SHEET_RUN, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_RESUME), "resume stays live");
    in.minutes = 6;
    in.paused = 0;
    zk_layout_rects_of(ZK_SCREEN_SHEET_RUN, &in, &R);
    TEQ_I(R.sheet.x, 16);
    TEQ_I(R.sheet.y, 16);
    TEQ_I(R.sheet.w, 768);
    TEQ_I(R.sheet.h, 332);
    TEQ_I(R.minus.w, 96);
    TEQ_I(R.minus.h, 96);
    TEQ_I(R.run.x, 600);
    TEQ_I(R.run.w, 176);
    TEQ_I(R.run.h, 96);

    n = zk_layout_targets(ZK_SCREEN_SHEET_STOP_ALL, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_CANCEL) && has_id(t, n, ZK_TARGET_STOP_ALL), "stop all");
    TCHECK(!has_id(t, n, ZK_TARGET_STOP), "full sheet covers STOP");
    TEQ_I(zk_layout_hit_is_stop(ZK_SCREEN_SHEET_STOP_ALL, &in, 594, 412), 0);
    zk_layout_rects_of(ZK_SCREEN_SHEET_STOP_ALL, &in, &R);
    TEQ_I(R.sheet.h, 448);
    TEQ_I(R.stop_all.h, 112);
    TEQ_I(R.cancel.h, 112);

    n = zk_layout_targets(ZK_SCREEN_SHEET_STOP_STATION, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_CANCEL), "running sheet cancel");
    TCHECK(has_id(t, n, ZK_TARGET_STOP), "running sheet STOP");
    TCHECK(!has_id(t, n, ZK_TARGET_STOP_ALL), "running sheet is not stop-all");
    TCHECK(!has_id(t, n, ZK_TARGET_RUN), "running sheet does not run");
    TEQ_I((int)zk_hit_test(t, n, 582, 280), (int)ZK_TARGET_NONE);
    TEQ_I(zk_layout_hit_is_stop(ZK_SCREEN_SHEET_STOP_STATION, &in, 594, 412), 1);
    zk_layout_rects_of(ZK_SCREEN_SHEET_STOP_STATION, &in, &R);
    TEQ_I(R.sheet.x, 16);
    TEQ_I(R.sheet.y, 16);
    TEQ_I(R.sheet.w, 768);
    TEQ_I(R.sheet.h, 332);
    TEQ_I(R.cancel.x, 36);
    TEQ_I(R.cancel.y, 232);
    TEQ_I(R.cancel.w, 352);
    TEQ_I(R.cancel.h, 96);
    TEQ_I(R.sheet_title.x, 36);
    TEQ_I(R.sheet_title.y, 62);
    TEQ_I(R.sheet_title.w, 728);
    TEQ_I(R.sheet_title.h, 80);

    in.n_stations = 4;
    TEQ_I(zk_layout_hit_is_stop(ZK_SCREEN_HOME, &in, 594, 412), 1);
    TEQ_I(zk_layout_hit_is_stop(ZK_SCREEN_HOME, &in, 404, 360), 1);
    TEQ_I(zk_layout_hit_is_stop(ZK_SCREEN_HOME, &in, 783, 463), 1);
    TEQ_I(zk_layout_hit_is_stop(ZK_SCREEN_HOME, &in, 784, 412), 0);
    TEQ_I(zk_layout_hit_is_stop(ZK_SCREEN_HOME, &in, 200, 412), 0);
}

static void test_dimmed_stop_passes(void)
{
    static const enum zk_screen screens[] = {ZK_SCREEN_HOME, ZK_SCREEN_SHEET_RUN, ZK_SCREEN_SHEET_STOP_STATION,
                                              ZK_SCREEN_SHEET_STOP_ALL, ZK_SCREEN_SHEET_OVERLAP, ZK_SCREEN_SCHEDULES};
    char absent[64];
    int s;
    snprintf(absent, sizeof absent, "/tmp/zk-layout-power-absent-%d", (int)getpid());
    for (s = 0; s < (int)(sizeof screens / sizeof screens[0]); s++) {
        zk_layout_in in;
        zk_target t[32];
        int n, i;
        memset(&in, 0, sizeof in);
        in.n_stations = 4;
        in.minutes = 6;
        in.n_schedules = 3;
        if (screens[s] == ZK_SCREEN_HOME && s == 0) {
            in.offline = 0;
        }
        n = zk_layout_targets(screens[s], &in, t, 32);
        for (i = 0; i < n; i++) {
            int x = t[i].rect.x + t[i].rect.w / 2;
            int y = t[i].rect.y + t[i].rect.h / 2;
            int hit = zk_layout_hit_is_stop(screens[s], &in, x, y);
            zk_power_t p;
            zk_power_config_t c;
            zk_power_inputs_t zin;
            zk_power_touch_action_t act;
            TEQ_I(hit, t[i].id == ZK_TARGET_STOP ? 1 : 0);
            memset(&c, 0, sizeof c);
            c.dim_after_sec = 2;
            c.off_after_sec = 10;
            c.dim_level = 40;
            c.backlight_dir = absent;
            zk_power_init(&p, &c);
            memset(&zin, 0, sizeof zin);
            zk_power_tick(&p, 0, &zin);
            zk_power_tick(&p, 2000, &zin);
            TEQ_I((int)zk_power_state(&p), (int)ZK_POWER_DIMMED);
            act = zk_power_touch_event(&p, 2000, 1, hit);
            if (t[i].id == ZK_TARGET_STOP) {
                TEQ_I((int)act, (int)ZK_POWER_PASS);
            } else {
                TEQ_I((int)act, (int)ZK_POWER_SWALLOW);
            }
            TEQ_I((int)zk_power_state(&p), (int)ZK_POWER_ACTIVE);
        }
    }
}

static int same_rect(zk_rect a, zk_rect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void test_schedules_layout(void)
{
    zk_layout_in in;
    zk_layout_rects a;
    zk_layout_rects b;
    zk_target t[32];
    int n;
    memset(&in, 0, sizeof in);
    in.n_stations = 4;
    in.n_schedules = 2;
    zk_layout_rects_of(ZK_SCREEN_SCHEDULES, &in, &a);
    TEQ_I(a.sheet.x, 16);
    TEQ_I(a.sheet.y, 16);
    TEQ_I(a.sheet.w, 768);
    TEQ_I(a.sheet.h, 332);
    TEQ_I(a.next.x, 400);
    TEQ_I(a.next.y, 232);
    TEQ_I(a.next.w, 364);
    TEQ_I(a.next.h, 96);
    TEQ_I(a.hero.x, 16);
    TEQ_I(a.hero.y, 16);
    TEQ_I(a.hero.w, 376);
    TEQ_I(a.hero.h, 332);
    TEQ_I(a.stop.x, 404);
    TEQ_I(a.stop.w, 380);
    TEQ_I(a.stop.h, 104);
    TEQ_I(a.sched.w, 120);
    TEQ_I(a.pause.x, 148);
    TEQ_I(a.pause.w, 244);
    check_targets(ZK_SCREEN_SCHEDULES, &in, "schedules two");
    n = zk_layout_targets(ZK_SCREEN_SCHEDULES, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_STOP) && has_id(t, n, ZK_TARGET_HOME), "schedules bar");
    TCHECK(has_id(t, n, ZK_TARGET_PAUSE) && has_id(t, n, ZK_TARGET_SCHED_NEXT), "schedules pause and next");
    TCHECK(!has_id(t, n, ZK_TARGET_SCHEDULES) && !has_id(t, n, ZK_TARGET_TILE0), "no home entry or tiles");
    TEQ_I((int)zk_hit_test(t, n, 76, 412), (int)ZK_TARGET_HOME);
    TEQ_I((int)zk_hit_test(t, n, 582, 280), (int)ZK_TARGET_SCHED_NEXT);
    TEQ_I(zk_layout_hit_is_stop(ZK_SCREEN_SCHEDULES, &in, 594, 412), 1);
    TEQ_I(zk_layout_hit_is_stop(ZK_SCREEN_SCHEDULES, &in, 76, 412), 0);

    in.n_schedules = 1;
    n = zk_layout_targets(ZK_SCREEN_SCHEDULES, &in, t, 32);
    TCHECK(!has_id(t, n, ZK_TARGET_SCHED_NEXT), "one program has no Next");
    TCHECK(has_id(t, n, ZK_TARGET_HOME) && has_id(t, n, ZK_TARGET_PAUSE), "one program still has the bar");

    in.n_schedules = 2;
    in.paused = 1;
    n = zk_layout_targets(ZK_SCREEN_SCHEDULES, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_RESUME) && !has_id(t, n, ZK_TARGET_PAUSE), "schedules resume");

    in.paused = 0;
    in.offline = 1;
    n = zk_layout_targets(ZK_SCREEN_SCHEDULES, &in, t, 32);
    TCHECK(has_id(t, n, ZK_TARGET_HOME) && has_id(t, n, ZK_TARGET_STOP), "offline schedules can go home");
    TCHECK(has_id(t, n, ZK_TARGET_SCHED_NEXT), "offline browse stays live");
    TCHECK(!has_id(t, n, ZK_TARGET_PAUSE), "offline pause is not a target on the list");

    in.offline = 0;
    in.n_schedules = 3;
    in.n_stations = 4;
    zk_layout_rects_of(ZK_SCREEN_SCHEDULES, &in, &a);
    in.n_stations = 6;
    zk_layout_rects_of(ZK_SCREEN_SCHEDULES, &in, &b);
    TCHECK(same_rect(a.sched, b.sched) && same_rect(a.pause, b.pause) && same_rect(a.stop, b.stop),
           "station count moves the schedules bar");
    TCHECK(same_rect(a.sheet, b.sheet) && same_rect(a.next, b.next) && same_rect(a.hero, b.hero),
           "station count moves the schedules sheet");
    TEQ_I(a.n_tiles, 4);
    TEQ_I(b.n_tiles, 6);
    in.n_stations = 4;
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &a);
    in.n_stations = 6;
    zk_layout_rects_of(ZK_SCREEN_HOME, &in, &b);
    TCHECK(same_rect(a.sched, b.sched) && same_rect(a.pause, b.pause) && same_rect(a.stop, b.stop),
           "station count moves the home bar");
    TCHECK(same_rect(a.hero, b.hero), "station count moves the hero");

    in.n_stations = 4;
    in.minutes = 6;
    n = zk_layout_targets(ZK_SCREEN_SHEET_RUN, &in, t, 32);
    TCHECK(!has_id(t, n, ZK_TARGET_SCHEDULES) && !has_id(t, n, ZK_TARGET_HOME), "sheet covers schedules");
}

int main(void)
{
    test_min_tap_constant();
    test_grids();
    test_bar_and_sheets();
    test_schedules_layout();
    test_every_screen();
    test_dimmed_stop_passes();
    return zk_test_report("test_layout");
}
