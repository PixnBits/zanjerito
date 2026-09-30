#include "zk_test.h"
#include "zk_layout.h"
#include "zk_power.h"

#include <unistd.h>

static int overlap(zk_rect a, zk_rect b)
{
    int ax2 = a.x + a.w, ay2 = a.y + a.h;
    int bx2 = b.x + b.w, by2 = b.y + b.h;
    return a.x < bx2 && b.x < ax2 && a.y < by2 && b.y < ay2;
}

static int inside_screen(zk_rect r)
{
    return r.x >= 0 && r.y >= 0 && r.x + r.w <= ZK_SCREEN_W && r.y + r.h <= ZK_SCREEN_H
           && r.w > 0 && r.h > 0;
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

static int is_modal(enum zk_screen sc)
{
    return sc == ZK_SCREEN_CONFIRM_STOP || sc == ZK_SCREEN_CONFIRM_PAUSE;
}

static void check_one(enum zk_screen sc, int n_stations, int rain)
{
    zk_layout_in in;
    zk_target t[32];
    int n, i, j;
    const zk_target *stop;
    memset(&in, 0, sizeof(in));
    in.n_stations = n_stations;
    in.rain_visible = rain;
    in.n_schedules = 3;
    n = zk_layout_targets(sc, &in, t, 32);
    TCHECK(n > 0 && n <= 32, "n targets screen=%d n=%d", (int)sc, n);

    for (i = 0; i < n; i++) {
        TCHECK(inside_screen(t[i].rect), "inside sc=%d id=%d %d,%d %dx%d",
               (int)sc, (int)t[i].id, t[i].rect.x, t[i].rect.y, t[i].rect.w, t[i].rect.h);
        for (j = i + 1; j < n; j++) {
            TCHECK(!overlap(t[i].rect, t[j].rect), "overlap sc=%d %d vs %d",
                   (int)sc, (int)t[i].id, (int)t[j].id);
        }
    }

    if (is_modal(sc)) {
        TCHECK(!find_id(t, n, ZK_TARGET_STOP), "modal has no STOP");
        TEQ_I(n, 2);
        TCHECK(find_id(t, n, ZK_TARGET_MODAL_OK), "modal ok");
        TCHECK(find_id(t, n, ZK_TARGET_MODAL_CANCEL), "modal cancel");
        for (i = 0; i < n; i++) {
            TCHECK(t[i].rect.w >= ZK_MIN_TAP && t[i].rect.h >= ZK_MIN_TAP,
                   "modal tap %d", (int)t[i].id);
        }
    } else {
        stop = find_id(t, n, ZK_TARGET_STOP);
        TCHECK(stop, "STOP present sc=%d", (int)sc);
        TCHECK(stop->rect.h >= ZK_STOP_MIN_H, "STOP h=%d sc=%d", stop->rect.h, (int)sc);
        if (sc == ZK_SCREEN_RUNNING) {
            TEQ_I(stop->rect.h, 304);
        } else {
            TEQ_I(stop->rect.h, 264);
        }
        for (i = 0; i < n; i++) {
            TCHECK(stop->rect.h >= t[i].rect.h, "STOP tallest sc=%d vs id=%d h=%d",
                   (int)sc, (int)t[i].id, t[i].rect.h);
            if (t[i].id == ZK_TARGET_STOP) {
                continue;
            }
            TCHECK(t[i].rect.w >= ZK_MIN_TAP && t[i].rect.h >= ZK_MIN_TAP,
                   "tap sc=%d id=%d %dx%d", (int)sc, (int)t[i].id, t[i].rect.w, t[i].rect.h);
        }
        TCHECK(!find_id(t, n, ZK_TARGET_MODAL_OK), "no modal on non-modal");
    }

    /* hit test centers and edges */
    for (i = 0; i < n; i++) {
        zk_rect r = t[i].rect;
        int cx = r.x + r.w / 2;
        int cy = r.y + r.h / 2;
        TEQ_I(zk_hit_test(t, n, cx, cy), t[i].id);
        TEQ_I(zk_hit_test(t, n, r.x, r.y), t[i].id);                 /* inclusive */
        TEQ_I(zk_hit_test(t, n, r.x + r.w - 1, r.y + r.h - 1), t[i].id);
        TEQ_I(zk_hit_test(t, n, r.x + r.w, r.y), ZK_TARGET_NONE);    /* exclusive right */
        TEQ_I(zk_hit_test(t, n, r.x, r.y + r.h), ZK_TARGET_NONE);    /* exclusive bottom */
        if (r.x > 0) {
            TEQ_I(zk_hit_test(t, n, r.x - 1, cy), ZK_TARGET_NONE);
        }
    }
}

static void test_home_tiles(void)
{
    zk_layout_in in;
    zk_target t[32];
    int n;
    memset(&in, 0, sizeof(in));
    in.n_stations = 4;
    in.rain_visible = 1;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(find_id(t, n, ZK_TARGET_TILE0) && find_id(t, n, ZK_TARGET_TILE3), "4 tiles");
    TEQ_I(find_id(t, n, ZK_TARGET_TILE0)->rect.h, 126);
    in.rain_visible = 0;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TEQ_I(find_id(t, n, ZK_TARGET_TILE0)->rect.h, 160);
    in.n_stations = 3;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(find_id(t, n, ZK_TARGET_TILE2), "3rd tile");
    TCHECK(!find_id(t, n, ZK_TARGET_TILE3), "no 4th tile");
    in.n_stations = 1;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(find_id(t, n, ZK_TARGET_TILE0), "1 tile");
    TCHECK(!find_id(t, n, ZK_TARGET_TILE1), "no 2nd");
    in.n_stations = 6;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(find_id(t, n, ZK_TARGET_TILE5), "6 tiles");
    TCHECK(!find_id(t, n, ZK_TARGET_TILE6), "no 7th of 6");
    TCHECK(find_id(t, n, ZK_TARGET_TILE0)->rect.w >= ZK_MIN_TAP, "6-tile width");
    TCHECK(find_id(t, n, ZK_TARGET_TILE0)->rect.h >= ZK_MIN_TAP, "6-tile height");
    in.n_stations = 8;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    TCHECK(find_id(t, n, ZK_TARGET_TILE7), "8 tiles");
    TCHECK(find_id(t, n, ZK_TARGET_TILE0)->rect.w >= ZK_MIN_TAP &&
               find_id(t, n, ZK_TARGET_TILE0)->rect.h >= ZK_MIN_TAP,
           "8-tile min");
}

static void test_schedules_button(void)
{
    zk_layout_in in;
    zk_target t[32];
    int n;
    const zk_target *s;
    memset(&in, 0, sizeof in);
    in.n_stations = 4;
    in.rain_visible = 1;
    n = zk_layout_targets(ZK_SCREEN_HOME, &in, t, 32);
    s = find_id(t, n, ZK_TARGET_SCHEDULES);
    TCHECK(s, "home schedules");
    TCHECK(s && s->rect.w >= ZK_MIN_TAP && s->rect.h >= ZK_MIN_TAP, "home schedules %dx%d",
           s ? s->rect.w : 0, s ? s->rect.h : 0);
    n = zk_layout_targets(ZK_SCREEN_PAUSED, &in, t, 32);
    s = find_id(t, n, ZK_TARGET_SCHEDULES);
    TCHECK(s, "paused schedules");
    TCHECK(s && s->rect.w >= ZK_MIN_TAP && s->rect.h >= ZK_MIN_TAP, "paused schedules %dx%d",
           s ? s->rect.w : 0, s ? s->rect.h : 0);
    n = zk_layout_targets(ZK_SCREEN_RUNNING, &in, t, 32);
    TCHECK(!find_id(t, n, ZK_TARGET_SCHEDULES), "running has no schedules");
}

static void test_station_sheet(void)
{
    zk_layout_in in;
    zk_target t[32];
    int n;
    const zk_target *stop;
    const zk_target *close;
    memset(&in, 0, sizeof in);
    in.n_stations = 4;
    n = zk_layout_targets(ZK_SCREEN_STATION, &in, t, 32);
    stop = find_id(t, n, ZK_TARGET_STOP);
    close = find_id(t, n, ZK_TARGET_BACK);
    TCHECK(stop && stop->rect.h >= ZK_STOP_MIN_H, "station STOP");
    TCHECK(close && close->rect.w >= ZK_MIN_TAP && close->rect.h >= ZK_MIN_TAP, "station Close");
    TEQ_I(stop ? stop->rect.h : 0, 264);
}

static void test_picker_schedules(void)
{
    zk_layout_in in;
    zk_target t[32];
    zk_layout_rects R;
    int n;
    memset(&in, 0, sizeof(in));
    in.n_stations = 4;
    n = zk_layout_targets(ZK_SCREEN_PICKER, &in, t, 32);
    TCHECK(find_id(t, n, ZK_TARGET_CHIP0) && find_id(t, n, ZK_TARGET_CHIP3), "chips");
    TCHECK(find_id(t, n, ZK_TARGET_BACK), "picker back");
    n = zk_layout_targets(ZK_SCREEN_SCHEDULES, &in, t, 32);
    TCHECK(find_id(t, n, ZK_TARGET_HOLD_EDIT), "hold");
    TCHECK(find_id(t, n, ZK_TARGET_BACK), "sched back");
    TEQ_I(find_id(t, n, ZK_TARGET_HOLD_EDIT)->rect.h, 114);
    zk_layout_rects_of(ZK_SCREEN_PICKER, &in, &R);
    TEQ_I(R.chips[0].h, 188);
    TEQ_I(R.chips[0].w, 255);
}

static void test_stop_hit_and_dim_rule(void)
{
    static const enum zk_screen screens[] = {
        ZK_SCREEN_HOME, ZK_SCREEN_RUNNING, ZK_SCREEN_PAUSED, ZK_SCREEN_PICKER,
        ZK_SCREEN_SCHEDULES, ZK_SCREEN_STATION, ZK_SCREEN_NEEDS_UPDATE,
        ZK_SCREEN_CONFIRM_STOP, ZK_SCREEN_CONFIRM_PAUSE
    };
    char absent[64];
    int s;

    snprintf(absent, sizeof absent, "/tmp/zk-layout-power-absent-%d", (int)getpid());
    for (s = 0; s < (int)(sizeof screens / sizeof screens[0]); s++) {
        zk_layout_in in;
        zk_target t[32];
        int n;
        int i;

        memset(&in, 0, sizeof in);
        in.n_stations = 4;
        in.rain_visible = 1;
        in.n_schedules = 3;
        n = zk_layout_targets(screens[s], &in, t, 32);
        for (i = 0; i < n; i++) {
            int x = t[i].rect.x + t[i].rect.w / 2;
            int y = t[i].rect.y + t[i].rect.h / 2;
            int hit = zk_layout_hit_is_stop(screens[s], &in, x, y);
            enum zk_target_id id = zk_hit_test(t, n, x, y);
            zk_power_t p;
            zk_power_config_t c;
            zk_power_inputs_t zin;
            zk_power_touch_action_t act;

            TEQ_I(hit, id == ZK_TARGET_STOP ? 1 : 0);
            TEQ_I((int)id, (int)t[i].id);
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

int main(void)
{
    static const enum zk_screen screens[] = {
        ZK_SCREEN_HOME, ZK_SCREEN_RUNNING, ZK_SCREEN_PAUSED,
        ZK_SCREEN_PICKER, ZK_SCREEN_SCHEDULES, ZK_SCREEN_STATION,
        ZK_SCREEN_NEEDS_UPDATE,
        ZK_SCREEN_CONFIRM_STOP, ZK_SCREEN_CONFIRM_PAUSE
    };
    int s, nst, rain;
    for (s = 0; s < (int)(sizeof(screens) / sizeof(screens[0])); s++) {
        for (rain = 0; rain <= 1; rain++) {
            for (nst = 1; nst <= 8; nst++) {
                check_one(screens[s], nst, rain);
            }
        }
    }
    test_home_tiles();
    test_picker_schedules();
    test_schedules_button();
    test_station_sheet();
    test_stop_hit_and_dim_rule();
    TEQ_I(ZK_PX_PER_INCH, 267);
    TCHECK((int)(0.4 * ZK_PX_PER_INCH + 0.5) == ZK_MIN_TAP || ZK_MIN_TAP == 107, "0.4in");
    return zk_test_report("test_layout");
}
