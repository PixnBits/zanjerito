#include "zk_test.h"
#include "zk_layout.h"

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
    TCHECK(find_id(t, n, ZK_TARGET_TILE3), "clamp still has 4");
    TCHECK(!find_id(t, n, ZK_TARGET_TILE4), "no 5th when 3-row too short");
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

int main(void)
{
    static const enum zk_screen screens[] = {
        ZK_SCREEN_HOME, ZK_SCREEN_RUNNING, ZK_SCREEN_PAUSED,
        ZK_SCREEN_PICKER, ZK_SCREEN_SCHEDULES,
        ZK_SCREEN_CONFIRM_STOP, ZK_SCREEN_CONFIRM_PAUSE
    };
    int s, nst, rain;
    for (s = 0; s < (int)(sizeof(screens) / sizeof(screens[0])); s++) {
        for (rain = 0; rain <= 1; rain++) {
            for (nst = 1; nst <= 4; nst++) {
                check_one(screens[s], nst, rain);
            }
        }
    }
    test_home_tiles();
    test_picker_schedules();
    TEQ_I(ZK_PX_PER_INCH, 267);
    TCHECK((int)(0.4 * ZK_PX_PER_INCH + 0.5) == ZK_MIN_TAP || ZK_MIN_TAP == 107, "0.4in");
    return zk_test_report("test_layout");
}
