#include "zk_test.h"

#include "data.h"
#include "generated/zk_fonts.h"
#include "platform.h"
#include "ui.h"
#include "zk_layout.h"

#include <sys/stat.h>

static int ends_ellipsis(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    return n >= 3 && memcmp(s + n - 3, "\xE2\x80\xA6", 3) == 0;
}

static int prefix_ellipsis(const char *shown, const char *full)
{
    size_t n;
    size_t fn;
    if (!shown || !full) {
        return 0;
    }
    n = strlen(shown);
    fn = strlen(full);
    if (n < 3 || memcmp(shown + n - 3, "\xE2\x80\xA6", 3) != 0) {
        return 0;
    }
    n -= 3;
    return n < fn && memcmp(shown, full, n) == 0;
}

static void make_title(char *dst, int n)
{
    char base[32];
    size_t bl;
    size_t i;
    snprintf(base, sizeof base, "TestStation%02d", n);
    bl = strlen(base);
    for (i = 0; i < 120; i++) {
        dst[i] = base[i % bl];
    }
    dst[120] = 0;
}

static const zk_ui_label_box_t *find_box(const zk_ui_label_box_t *b, int n, const char *id)
{
    int i;
    for (i = 0; i < n; i++) {
        if (strcmp(b[i].id, id) == 0) {
            return &b[i];
        }
    }
    return NULL;
}

static int overlaps(const zk_ui_label_box_t *a, const zk_ui_label_box_t *b)
{
    if (a->x + a->w <= b->x || b->x + b->w <= a->x) {
        return 0;
    }
    if (a->y + a->h <= b->y || b->y + b->h <= a->y) {
        return 0;
    }
    return 1;
}

static int inside_parent(const zk_ui_label_box_t *a)
{
    return a->x >= a->parent_x && a->y >= a->parent_y && a->x + a->w <= a->parent_x + a->parent_w &&
           a->y + a->h <= a->parent_y + a->parent_h;
}

static int load_fix(const char *name, int screen)
{
    char path[512];
    zk_app_t app;
    struct stat st;
    const char *roots[] = {"tests/fixtures", "native-kiosk/tests/fixtures", NULL};
    int i;
    path[0] = 0;
    for (i = 0; roots[i]; i++) {
        snprintf(path, sizeof path, "%s/%s", roots[i], name);
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            break;
        }
    }
    if (!roots[i]) {
        return -1;
    }
    zk_data_stop();
    memset(&app, 0, sizeof app);
    app.fixture_dir = path;
    app.live_clock = 0;
    zk_ui_set_fixture(path);
    if (zk_data_start(&app) != 0) {
        return -1;
    }
    zk_ui_debug_set_view(screen, 0, -1);
    zk_ui_refresh();
    return 0;
}

static int grab(zk_ui_label_box_t *b, int cap)
{
    int n = zk_ui_debug_label_boxes(b, cap);
    TCHECK(n > 0, "label boxes %d", n);
    return n;
}

static void check_geometry(const zk_ui_label_box_t *b, int n, const char *screen)
{
    int i, j;
    int clamped = 0;
    for (i = 0; i < n; i++) {
        if (!b[i].visible || !b[i].clamped) {
            continue;
        }
        clamped++;
        TCHECK(inside_parent(&b[i]), "%s %s outside parent (%d,%d %dx%d) parent (%d,%d %dx%d)", screen, b[i].id,
               b[i].x, b[i].y, b[i].w, b[i].h, b[i].parent_x, b[i].parent_y, b[i].parent_w, b[i].parent_h);
        for (j = 0; j < n; j++) {
            if (i == j || !b[j].visible) {
                continue;
            }
            TCHECK(!overlaps(&b[i], &b[j]), "%s %s overlaps %s (%d,%d %dx%d) vs (%d,%d %dx%d)", screen, b[i].id,
                   b[j].id, b[i].x, b[i].y, b[i].w, b[i].h, b[j].x, b[j].y, b[j].w, b[j].h);
        }
    }
    TCHECK(clamped > 0, "%s has no clamped labels", screen);
}

static int dark_px(const uint8_t *p)
{
    int b = p[0];
    int g = p[1];
    int r = p[2];
    return (0xFC - r) > 40 && (0xF5 - g) > 40 && (0xE6 - b) > 30;
}

static void check_pause_gap(const zk_ui_label_box_t *name, const zk_ui_label_box_t *sub)
{
    const uint8_t *frame;
    int fw, fh, stride;
    int y0, y1, x0, x1, y, x;
    int dark = 0;
    int ink = 0;
    TCHECK(name && sub, "pause labels");
    if (!name || !sub) {
        return;
    }
    TCHECK(name->y_rel == 54, "info_name y_rel %d", name->y_rel);
    TCHECK(sub->y_rel == 112, "info_sub0 y_rel %d", sub->y_rel);
    TCHECK(name->h > 0 && name->y_rel + name->h <= 112, "info_name bottom %d", name->y_rel + name->h);
    TCHECK(name->y + name->h <= sub->y, "info_name abs bottom %d sub %d", name->y + name->h, sub->y);
    TCHECK(sub->y - (name->y + name->h) >= 8, "gap %d", sub->y - (name->y + name->h));
    frame = zk_platform_frame(&fw, &fh, &stride);
    TCHECK(frame != NULL, "frame");
    if (!frame) {
        return;
    }
    y0 = name->y + name->h;
    y1 = sub->y;
    x0 = name->x;
    x1 = name->x + name->w;
    TCHECK(y0 >= 0 && y1 <= fh && x0 >= 0 && x1 <= fw, "gap outside frame");
    for (y = name->y; y < name->y + name->h && y < fh; y++) {
        for (x = x0; x < x1 && x < fw; x++) {
            if (y >= 0 && x >= 0 && dark_px(frame + (size_t)y * (size_t)stride + (size_t)x * 4u)) {
                ink++;
            }
        }
    }
    TCHECK(ink > 0, "info_name drew no ink");
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (dark_px(frame + (size_t)y * (size_t)stride + (size_t)x * 4u)) {
                if (dark < 3) {
                    fprintf(stderr, "dark gap pixel %d,%d\n", x, y);
                }
                dark++;
            }
        }
    }
    TCHECK(dark == 0, "pause gap has %d dark pixels", dark);
}

static void test_paused_short(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *name;
    const zk_ui_label_box_t *sub;
    TCHECK(load_fix("paused-manual", ZK_SCREEN_PAUSED) == 0, "load paused-manual");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, "paused-manual");
    name = find_box(b, n, "info_name");
    sub = find_box(b, n, "info_sub0");
    TCHECK(name != NULL, "short info_name");
    if (name) {
        TEQ_S(name->text, "Runs again when you resume");
        TCHECK(!ends_ellipsis(name->text), "short name ellipsized: %s", name->text);
    }
    check_pause_gap(name, sub);
}

static void test_paused_long(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *name;
    const zk_ui_label_box_t *sub;
    const char *full =
        "live parity: alpha 2, beta 3, gamma 3 (bash cron 08:23) \xC2\xB7 Daily 8:23 AM \xC2\xB7 Tomorrow 8:23 AM";
    TCHECK(load_fix("paused-long", ZK_SCREEN_PAUSED) == 0, "load paused-long");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, "paused-long");
    name = find_box(b, n, "info_name");
    sub = find_box(b, n, "info_sub0");
    TCHECK(name != NULL, "long info_name");
    if (name) {
        TCHECK(strncmp(name->text, "live parity:", 12) == 0, "long name %s", name->text);
        TCHECK(prefix_ellipsis(name->text, full), "long name not an ellipsized prefix: %s", name->text);
        TCHECK(strlen(name->text) < strlen(full), "long name not shorter");
    }
    check_pause_gap(name, sub);
}

static void test_home_long(void)
{
    zk_ui_label_box_t b[96];
    int n;
    int i;
    const zk_ui_label_box_t *kicker;
    const zk_ui_label_box_t *big;
    TCHECK(load_fix("home-longnames", ZK_SCREEN_HOME) == 0, "load home-longnames");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, "home-longnames");
    kicker = find_box(b, n, "hdr_kicker");
    big = find_box(b, n, "hdr_big");
    TCHECK(kicker && big, "header labels");
    if (kicker) {
        TEQ_S(kicker->text, "NEXT RUN \xC2\xB7 MORNING CYCLE");
    }
    if (big) {
        TEQ_S(big->text, "Today 8:23 AM");
    }
    for (i = 0; i < 4; i++) {
        char id[32];
        char full[128];
        const zk_ui_label_box_t *t;
        const zk_ui_label_box_t *st;
        snprintf(id, sizeof id, "tile_title%d", i);
        make_title(full, i + 1);
        t = find_box(b, n, id);
        TCHECK(t != NULL, "missing %s", id);
        if (t) {
            TCHECK(prefix_ellipsis(t->text, full), "%s text %s", id, t->text);
        }
        {
            zk_ui_tile_dbg_t tile;
            TCHECK(zk_ui_debug_tile(i, &tile) == 1, "tile %d", i);
            TEQ_I(tile.on, 0);
            TEQ_I(tile.bg, 0xFCF5E6);
            TEQ_I(tile.border_w, 3);
            TEQ_I(tile.border, 0xD2B78A);
            TEQ_I(tile.drop_visible, 0);
            TEQ_I(tile.title_w, tile.tile_w - 28);
        }
        snprintf(id, sizeof id, "tile_state%d", i);
        st = find_box(b, n, id);
        TCHECK(st == NULL, "pill %s still present", id);
    }
}

static int white_px(const uint8_t *p)
{
    int b = p[0];
    int g = p[1];
    int r = p[2];
    return r > 220 && g > 220 && b > 220;
}

/* Visual gap between the STOP octagon and the word, in the center of the button. */
static void check_stop_gap(const zk_ui_label_box_t *b, int n)
{
    const zk_ui_label_box_t *word = find_box(b, n, "stop_word");
    const uint8_t *frame;
    int fw, fh, stride;
    int x0, x1, x;
    int best = 1000;
    int seen = 0;
    TCHECK(word != NULL, "stop word");
    if (!word) {
        return;
    }
    frame = zk_platform_frame(&fw, &fh, &stride);
    TCHECK(frame != NULL, "stop frame");
    if (!frame) {
        return;
    }
    x0 = word->x + word->w / 2 - 20;
    x1 = word->x + word->w / 2 + 20;
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > fw) {
        x1 = fw;
    }
    for (x = x0; x < x1; x++) {
        int y;
        int text_top = -1;
        int icon_bot = -1;
        int y_hi = word->y + word->h;
        if (y_hi > fh) {
            y_hi = fh;
        }
        for (y = word->y; y < y_hi; y++) {
            const uint8_t *p = frame + (size_t)y * (size_t)stride + (size_t)x * 4u;
            if (white_px(p)) {
                text_top = y;
                break;
            }
        }
        if (text_top < 0) {
            continue;
        }
        for (y = text_top - 1; y >= 0 && y >= text_top - 80; y--) {
            const uint8_t *p = frame + (size_t)y * (size_t)stride + (size_t)x * 4u;
            if (white_px(p)) {
                icon_bot = y;
                break;
            }
        }
        if (icon_bot < 0) {
            continue;
        }
        {
            int gap = text_top - icon_bot - 1;
            if (gap < best) {
                best = gap;
            }
            seen++;
        }
    }
    TCHECK(seen > 0, "stop icon and word ink");
    TCHECK(best >= 8, "stop gap %d px", best);
}

static void test_home_onoff(void)
{
    zk_ui_label_box_t b[96];
    int n;
    int i;
    const zk_ui_label_box_t *kicker;
    const zk_ui_label_box_t *big;
    const char *full = "NEXT RUN \xC2\xB7 MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM";
    TCHECK(load_fix("home-onoff", ZK_SCREEN_HOME) == 0, "load home-onoff");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, "home-onoff");
    kicker = find_box(b, n, "hdr_kicker");
    big = find_box(b, n, "hdr_big");
    TCHECK(kicker && big, "onoff header");
    if (kicker) {
        TCHECK(strncmp(kicker->text, "NEXT RUN \xC2\xB7 ", 12) == 0, "prefix %s", kicker->text);
        TCHECK(prefix_ellipsis(kicker->text, full), "name not end-cut: %s", kicker->text);
    }
    if (big) {
        TEQ_S(big->text, "Today 6:10 AM");
        TCHECK(!ends_ellipsis(big->text), "time ellipsized");
    }
    for (i = 0; i < 4; i++) {
        char id[32];
        zk_ui_tile_dbg_t tile;
        snprintf(id, sizeof id, "tile_state%d", i);
        TCHECK(find_box(b, n, id) == NULL, "pill %s still present", id);
        TCHECK(zk_ui_debug_tile(i, &tile) == 1, "tile %d", i);
        if (i == 0) {
            TEQ_I(tile.on, 1);
            TEQ_I(tile.bg, 0x0F6E6C);
            TEQ_I(tile.border_w, 4);
            TEQ_I(tile.border, 0x0F6E6C);
            TEQ_I(tile.drop_visible, 1);
        } else {
            TEQ_I(tile.on, 0);
            TEQ_I(tile.bg, 0xFCF5E6);
            TEQ_I(tile.border_w, 3);
            TEQ_I(tile.drop_visible, 0);
        }
        TEQ_I(tile.title_w, tile.tile_w - 28);
    }
    for (i = 0; i < n; i++) {
        TCHECK(strcmp(b[i].text, "On") != 0 && strcmp(b[i].text, "Off") != 0, "pill text %s", b[i].text);
    }
    {
        int32_t timew = lv_text_get_width("Tomorrow 6:10 AM", (uint32_t)strlen("Tomorrow 6:10 AM"), &zk_font_xb_50, 0);
        if (big) {
            TCHECK(big->w >= timew, "time slot %d holds %d", big->w, (int)timew);
        }
    }
    check_stop_gap(b, n);
}

static void test_running_long(void)
{
    zk_ui_label_box_t b[96];
    int n;
    char cur[128];
    char next_full[160];
    const zk_ui_label_box_t *title;
    const zk_ui_label_box_t *next;
    const zk_ui_label_box_t *step;
    TCHECK(load_fix("running-long", ZK_SCREEN_RUNNING) == 0, "load running-long");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, "running-long");
    make_title(cur, 2);
    make_title(next_full, 4);
    memmove(next_full + 6, next_full, strlen(next_full) + 1);
    memcpy(next_full, "Next: ", 6);
    title = find_box(b, n, "run_title");
    next = find_box(b, n, "run_next");
    step = find_box(b, n, "run_step");
    TCHECK(title && next && step, "running labels");
    if (title) {
        TCHECK(prefix_ellipsis(title->text, cur), "run_title %s", title->text);
    }
    if (next) {
        TCHECK(prefix_ellipsis(next->text, next_full), "run_next %s", next->text);
        TCHECK(strncmp(next->text, "Next:", 5) == 0, "run_next start %s", next->text);
    }
    if (step) {
        TEQ_S(step->text, "Step 2 of 3");
    }
    check_stop_gap(b, n);
}

/* Main's station title slot is tile width minus 14 px insets (parent_w - 28).
 * Soil and no-soil must both keep that width. "Test Station N" fit on main. */
static void test_home_title_width(const char *fix)
{
    zk_ui_label_box_t b[96];
    int n;
    int i;
    TCHECK(load_fix(fix, ZK_SCREEN_HOME) == 0, "load %s", fix);
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, fix);
    for (i = 0; i < 4; i++) {
        char id[32];
        char want[32];
        const zk_ui_label_box_t *t;
        const zk_ui_label_box_t *st;
        snprintf(id, sizeof id, "tile_title%d", i);
        t = find_box(b, n, id);
        TCHECK(t != NULL, "%s missing %s", fix, id);
        if (t) {
            TEQ_I(t->w, t->parent_w - 28);
            snprintf(want, sizeof want, "Test Station %d", i + 1);
            TEQ_S(t->text, want);
        }
        snprintf(id, sizeof id, "tile_state%d", i);
        st = find_box(b, n, id);
        TCHECK(st == NULL, "%s pill %s still present", fix, id);
        {
            zk_ui_tile_dbg_t tile;
            TCHECK(zk_ui_debug_tile(i, &tile) == 1, "%s tile %d", fix, i);
            TEQ_I(tile.drop_visible, 0);
            TEQ_I(tile.bg, 0xFCF5E6);
            TEQ_I(tile.title_w, tile.tile_w - 28);
        }
    }
}

static void test_station_sheet_and_needs_update(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *title;
    const zk_ui_label_box_t *state;
    const zk_ui_label_box_t *exempt;
    const zk_ui_label_box_t *msg;
    const zk_ui_label_box_t *nu;
    const zk_ui_label_box_t *sched;
    TCHECK(load_fix("home-rain", ZK_SCREEN_HOME) == 0, "load home for schedules");
    n = grab(b, 96);
    sched = find_box(b, n, "sched_btn");
    TCHECK(sched != NULL, "schedules label");
    if (sched) {
        int32_t tw = lv_text_get_width("Schedules", (uint32_t)strlen("Schedules"), &zk_font_b_26, 0);
        TEQ_S(sched->text, "Schedules");
        TCHECK(inside_parent(sched), "schedules box");
        TCHECK(sched->parent_w >= tw + 24, "schedules pad parent %d text %d", sched->parent_w, (int)tw);
        TCHECK(sched->x >= sched->parent_x + 12, "schedules left inset %d", sched->x - sched->parent_x);
        TCHECK(sched->x + sched->w <= sched->parent_x + sched->parent_w - 12, "schedules right inset");
    }
    zk_ui_debug_set_view(ZK_SCREEN_STATION, 0, 0);
    zk_ui_refresh();
    n = grab(b, 96);
    check_geometry(b, n, "station-sheet");
    title = find_box(b, n, "st_title");
    state = find_box(b, n, "st_state");
    TCHECK(title && state, "sheet labels");
    if (title) {
        TEQ_S(title->text, "Test Station 1");
    }
    if (state) {
        TEQ_S(state->text, "Idle");
    }
    zk_ui_debug_set_view(ZK_SCREEN_STATION, 0, 1);
    zk_ui_refresh();
    n = grab(b, 96);
    exempt = find_box(b, n, "st_exempt");
    TCHECK(exempt != NULL, "exempt");
    if (exempt) {
        TCHECK(strstr(exempt->text, "Skips rain") != NULL, "exempt %s", exempt->text);
    }
    TCHECK(load_fix("home-stale", ZK_SCREEN_STATION) == 0, "stale sheet");
    zk_ui_debug_set_view(ZK_SCREEN_STATION, 0, 0);
    zk_ui_refresh();
    n = grab(b, 96);
    msg = find_box(b, n, "st_soil_msg");
    TCHECK(msg != NULL, "stale soil msg");
    if (msg) {
        TCHECK(strstr(msg->text, "ET data old") != NULL, "stale msg %s", msg->text);
    }
    TCHECK(load_fix("home-rain", ZK_SCREEN_NEEDS_UPDATE) == 0, "needs-update");
    zk_data_debug_force_needs_update(1);
    zk_ui_debug_set_view(ZK_SCREEN_NEEDS_UPDATE, 0, -1);
    zk_ui_refresh();
    n = grab(b, 96);
    check_geometry(b, n, "needs-update");
    nu = find_box(b, n, "nu_title");
    TCHECK(nu != NULL, "nu title");
    if (nu) {
        TEQ_S(nu->text, "Controller needs an update");
    }
    zk_data_debug_force_needs_update(0);
}

int main(void)
{
    zk_platform_opts_t opts;
    zk_app_t app;
    int rc;
    memset(&opts, 0, sizeof opts);
    memset(&app, 0, sizeof app);
    if (zk_platform_init(&opts) != 0) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }
    zk_ui_init(zk_platform_display(), &app);
    test_paused_short();
    test_paused_long();
    test_home_long();
    test_home_onoff();
    test_home_title_width("home-rain");
    test_home_title_width("home-nosoil");
    test_running_long();
    test_station_sheet_and_needs_update();
    zk_data_stop();
    lv_deinit();
    rc = zk_test_report("test_ui_text");
    return rc;
}
