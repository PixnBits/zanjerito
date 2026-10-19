#include "zk_test.h"

#include "data.h"
#include "platform.h"
#include "ui.h"
#include "zk_copy.h"
#include "zk_flow.h"
#include "zk_layout.h"

#include "generated/zk_fonts.h"

#include <sys/stat.h>

static int ends_ellipsis(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    return n >= 3 && memcmp(s + n - 3, "\xE2\x80\xA6", 3) == 0;
}

static void strip_breaks(char *dst, size_t cap, const char *s)
{
    size_t o = 0;
    if (!dst || cap == 0) {
        return;
    }
    if (!s) {
        s = "";
    }
    while (*s && o + 1 < cap) {
        if (*s != '\n') {
            dst[o++] = *s;
        }
        s++;
    }
    dst[o] = 0;
}

static int prefix_ellipsis(const char *shown, const char *full)
{
    char flat[ZK_UI_LABEL_TEXT_MAX];
    size_t n;
    size_t fn;
    if (!shown || !full) {
        return 0;
    }
    strip_breaks(flat, sizeof flat, shown);
    n = strlen(flat);
    fn = strlen(full);
    if (n < 3 || memcmp(flat + n - 3, "\xE2\x80\xA6", 3) != 0) {
        return 0;
    }
    n -= 3;
    return n < fn && memcmp(flat, full, n) == 0;
}

/* Newlines from wrapping stand in for the spaces they replaced. */
static int wrap_prefix(const char *shown, const char *full)
{
    char flat[ZK_UI_LABEL_TEXT_MAX];
    size_t o = 0;
    const char *s = shown ? shown : "";
    while (*s && o + 1 < sizeof flat) {
        flat[o++] = (*s == '\n') ? ' ' : *s;
        s++;
    }
    flat[o] = 0;
    return prefix_ellipsis(flat, full);
}

static size_t kept_prefix(const char *shown)
{
    char flat[ZK_UI_LABEL_TEXT_MAX];
    size_t n;
    strip_breaks(flat, sizeof flat, shown);
    n = strlen(flat);
    if (n >= 3 && memcmp(flat + n - 3, "\xE2\x80\xA6", 3) == 0) {
        n -= 3;
    }
    return n;
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

static void assert_hero_clear_of_rain(const zk_ui_label_box_t *b, int n, const char *screen);
static void assert_rain_end(const zk_ui_label_box_t *b, int n, const char *screen);
static void assert_slots_fixed(const zk_ui_label_box_t *b, int n, int nstat);

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
    zk_data_debug_force_stale(0);
    zk_data_debug_force_needs_update(0);
    zk_ui_debug_set_stop(-1);
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

/* Clamped labels stay inside their parent and clear of every other visible label.
 * require_clamped is 1 only when this screen is expected to ellipsize something. */
static void check_geometry(const zk_ui_label_box_t *b, int n, const char *screen, int require_clamped)
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
    if (require_clamped) {
        TCHECK(clamped > 0, "%s has no clamped labels", screen);
    }
}

/* White ink gap between the STOP octagon and the word, along the word's mid line. */
static int white_px(const uint8_t *p)
{
    return p[2] > 220 && p[1] > 220 && p[0] > 220;
}

static void check_stop_gap(const zk_ui_label_box_t *b, int n)
{
    const zk_ui_label_box_t *word = find_box(b, n, "stop_word");
    const uint8_t *frame;
    int fw, fh, stride;
    int y;
    int x;
    int x0;
    int x1;
    int icon = -1;
    int text = -1;
    TCHECK(word != NULL, "stop word");
    if (!word) {
        return;
    }
    frame = zk_platform_frame(&fw, &fh, &stride);
    TCHECK(frame != NULL, "stop frame");
    if (!frame) {
        return;
    }
    y = word->y + word->h / 2;
    if (y < 0 || y >= fh) {
        TCHECK(0, "stop word y %d", y);
        return;
    }
    x0 = word->parent_x;
    x1 = word->x + word->w;
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > fw) {
        x1 = fw;
    }
    for (x = x0; x < x1; x++) {
        const uint8_t *p = frame + (size_t)y * (size_t)stride + (size_t)x * 4u;
        if (!white_px(p)) {
            continue;
        }
        if (x < word->x) {
            icon = x;
        } else if (text < 0) {
            text = x;
        }
    }
    TCHECK(icon >= 0 && text >= 0, "stop icon and word ink");
    if (icon >= 0 && text >= 0) {
        TCHECK(text - icon - 1 >= 8, "stop gap %d px", text - icon - 1);
    }
}

static void test_paused_short(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *label;
    const zk_ui_label_box_t *head;
    const zk_ui_label_box_t *sub;
    const zk_ui_label_box_t *bar;
    TCHECK(load_fix("paused-manual", ZK_SCREEN_HOME) == 0, "load paused-manual");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    label = find_box(b, n, "hero_label");
    head = find_box(b, n, "hero_headline");
    sub = find_box(b, n, "hero_secondary");
    bar = find_box(b, n, "pause_word");
    TCHECK(label && head && sub && bar, "paused labels");
    if (label) {
        TEQ_S(label->text, "PAUSED FOR RAIN");
    }
    if (head) {
        TEQ_S(head->text, "Until you resume");
        TCHECK(!ends_ellipsis(head->text), "headline ellipsized");
    }
    if (sub) {
        TEQ_S(sub->text, "Tap a station to run it anyway");
    }
    {
        const zk_ui_label_box_t *time = find_box(b, n, "hero_time");
        if (time) {
            TCHECK(!time->visible, "paused hero still draws a time line");
        }
    }
    if (bar) {
        TEQ_S(bar->text, "Resume");
    }
}

static void test_paused_long(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *label;
    const zk_ui_label_box_t *head;
    TCHECK(load_fix("paused-long", ZK_SCREEN_HOME) == 0, "load paused-long");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    label = find_box(b, n, "hero_label");
    head = find_box(b, n, "hero_headline");
    TCHECK(label && head, "paused-long labels");
    if (label) {
        TEQ_S(label->text, "PAUSED FOR RAIN");
    }
    if (head) {
        TCHECK(strncmp(head->text, "Until ", 6) == 0, "headline %s", head->text);
        TCHECK(strstr(head->text, ZK_NBSP "AM") != NULL, "headline time %s", head->text);
        TCHECK(!ends_ellipsis(head->text), "headline ellipsized: %s", head->text);
    }
    {
        const zk_ui_label_box_t *time = find_box(b, n, "hero_time");
        if (time) {
            TCHECK(!time->visible, "paused-long still draws a time line");
        }
    }
}

static void test_home_long(void)
{
    zk_ui_label_box_t b[96];
    int n;
    int i;
    const zk_ui_label_box_t *label;
    const zk_ui_label_box_t *head;
    char today[64];
    TCHECK(load_fix("home-longnames", ZK_SCREEN_HOME) == 0, "load home-longnames");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, "home-longnames", 1);
    label = find_box(b, n, "hero_label");
    head = find_box(b, n, "hero_headline");
    TCHECK(label && head, "header labels");
    if (label) {
        TEQ_S(label->text, "NEXT RUN " ZK_MID " MORNING CYCLE");
    }
    snprintf(today, sizeof today, "Today 8:23" ZK_NBSP "AM");
    if (head) {
        TEQ_S(head->text, today);
        TCHECK(!ends_ellipsis(head->text), "headline ellipsized");
    }
    for (i = 0; i < 4; i++) {
        char id[32];
        char full[128];
        const zk_ui_label_box_t *t;
        zk_ui_tile_dbg_t tile;
        snprintf(id, sizeof id, "tile_title%d", i);
        make_title(full, i + 1);
        t = find_box(b, n, id);
        TCHECK(t != NULL, "missing %s", id);
        if (t) {
            TCHECK(prefix_ellipsis(t->text, full), "%s text %s", id, t->text);
            TCHECK(kept_prefix(t->text) >= 15, "%s kept %zu of %s", id, kept_prefix(t->text), t->text);
        }
        TCHECK(zk_ui_debug_tile(i, &tile) == 1, "tile %d", i);
        TEQ_I(tile.on, 0);
        TEQ_I(tile.bg, 0xFCF5E6);
        TEQ_I(tile.border_w, 2);
        TEQ_I(tile.border, 0xD9C3A0);
        TEQ_I(tile.drop_visible, 0);
        TEQ_I(tile.title_w, tile.tile_w - 28);
    }
    assert_slots_fixed(b, n, 4);
}

static void test_home_onoff(void)
{
    zk_ui_label_box_t b[96];
    int n;
    int i;
    const zk_ui_label_box_t *label;
    const zk_ui_label_box_t *head;
    const zk_ui_label_box_t *time;
    char ms[41];
    char full[96];
    memset(ms, 'M', 40);
    ms[40] = 0;
    snprintf(full, sizeof full, "WATERING NOW " ZK_MID " %s", ms);
    TCHECK(load_fix("home-onoff", ZK_SCREEN_HOME) == 0, "load home-onoff");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, "home-onoff", 0);
    label = find_box(b, n, "hero_label");
    head = find_box(b, n, "hero_headline");
    time = find_box(b, n, "hero_time");
    TCHECK(label && head && time, "onoff header");
    if (label) {
        TCHECK(strncmp(label->text, "WATERING NOW " ZK_MID " ", 16) == 0, "prefix %s", label->text);
        TCHECK(strcmp(label->text, full) == 0 || prefix_ellipsis(label->text, full), "label %s", label->text);
    }
    if (head) {
        lv_point_t sz;
        char flat[64];
        strip_breaks(flat, sizeof flat, head->text);
        lv_text_get_size(&sz, ms, &zk_font_lit_48, 0, 0, 332, LV_TEXT_FLAG_NONE);
        TEQ_I(head->w, 332);
        TEQ_I(head->x - head->parent_x, 24);
        if ((int)sz.y > 52 * 3) {
            TCHECK(prefix_ellipsis(head->text, ms), "headline %s", head->text);
        } else {
            TEQ_S(flat, ms);
            TCHECK(!ends_ellipsis(head->text), "headline ellipsized");
        }
    }
    if (time) {
        TCHECK(time->visible, "time hidden");
        TCHECK(strstr(time->text, "min left") != NULL, "time %s", time->text);
    }
    for (i = 0; i < 4; i++) {
        zk_ui_tile_dbg_t tile;
        TCHECK(zk_ui_debug_tile(i, &tile) == 1, "tile %d", i);
        if (i == 0) {
            TEQ_I(tile.on, 1);
            TEQ_I(tile.bg, 0x0F6E6C);
            TEQ_I(tile.border_w, 2);
            TEQ_I(tile.border, 0x0F6E6C);
            TEQ_I(tile.drop_visible, 1);
        } else {
            TEQ_I(tile.on, 0);
            TEQ_I(tile.bg, 0xFCF5E6);
            TEQ_I(tile.border_w, 2);
            TEQ_I(tile.border, 0xD9C3A0);
            TEQ_I(tile.drop_visible, 0);
        }
        TEQ_I(tile.title_w, tile.tile_w - 28);
    }
    for (i = 0; i < n; i++) {
        TCHECK(strcmp(b[i].text, "On") != 0 && strcmp(b[i].text, "Off") != 0, "pill text %s", b[i].text);
    }
    check_stop_gap(b, n);
    {
        const zk_ui_label_box_t *rain = find_box(b, n, "rain_text");
        /* A three-line clamp that meets the meter hides the rain before the ends line. */
        if (rain && rain->visible) {
            assert_hero_clear_of_rain(b, n, "home-onoff");
            assert_rain_end(b, n, "home-onoff");
        } else if (time) {
            TCHECK(time->visible, "time dropped when rain yielded");
        }
    }
}

static int box_hits(const zk_ui_label_box_t *a, int x, int y, int w, int h)
{
    if (!a || !a->visible || a->w <= 0 || a->h <= 0 || w <= 0 || h <= 0) {
        return 0;
    }
    if (a->x + a->w <= x || x + w <= a->x) {
        return 0;
    }
    if (a->y + a->h <= y || y + h <= a->y) {
        return 0;
    }
    return 1;
}

static void assert_rain_end(const zk_ui_label_box_t *b, int n, const char *screen)
{
    const zk_ui_label_box_t *end = find_box(b, n, "rain_1");
    TCHECK(end && end->visible, "%s rain end label", screen);
    if (!end) {
        return;
    }
    TEQ_S(end->text, "1 in");
    TEQ_I(end->parent_x, ZK_RAIN_X);
    TEQ_I(end->x, ZK_RAIN_X + 268);
    TEQ_I(end->w, 60);
    TEQ_I(end->x + end->w, ZK_RAIN_X + ZK_RAIN_TRACK);
}

static void assert_hero_clear_of_rain(const zk_ui_label_box_t *b, int n, const char *screen)
{
    const zk_ui_label_box_t *head = find_box(b, n, "hero_headline");
    const zk_ui_label_box_t *time = find_box(b, n, "hero_time");
    const zk_ui_label_box_t *rain = find_box(b, n, "rain_text");
    TCHECK(head && time && rain, "%s hero boxes", screen);
    if (!head || !time || !rain) {
        return;
    }
    TCHECK(head->visible, "%s headline hidden", screen);
    TCHECK(time->visible, "%s time hidden", screen);
    TEQ_I(rain->parent_x, ZK_RAIN_X);
    TEQ_I(rain->parent_y, ZK_RAIN_Y);
    TEQ_I(rain->parent_w, ZK_RAIN_W);
    TEQ_I(rain->parent_h, ZK_RAIN_H);
    TCHECK(!overlaps(head, time), "%s headline overlaps time (%d,%d %dx%d) vs (%d,%d %dx%d)", screen, head->x, head->y,
           head->w, head->h, time->x, time->y, time->w, time->h);
    TCHECK(!box_hits(head, rain->parent_x, rain->parent_y, rain->parent_w, rain->parent_h),
           "%s headline overlaps rain", screen);
    TCHECK(!box_hits(time, rain->parent_x, rain->parent_y, rain->parent_w, rain->parent_h), "%s time overlaps rain",
           screen);
}

static void assert_slots_fixed(const zk_ui_label_box_t *b, int n, int nstat)
{
    int i;
    int shown = nstat > 4 ? 6 : 4;
    if (nstat < shown) {
        shown = nstat;
    }
    for (i = 0; i < shown; i++) {
        char id[32];
        zk_rect r;
        const zk_ui_label_box_t *slot;
        snprintf(id, sizeof id, "tile_slot%d", i);
        zk_layout_slot(nstat > 4 ? 6 : 4, i, &r);
        slot = find_box(b, n, id);
        TCHECK(slot != NULL, "slot %d", i);
        if (!slot) {
            continue;
        }
        TEQ_I(slot->parent_x, r.x);
        /* The slot sits at its spec rect; a name that needs its extra clamp
         * lines (spec screens) pushes it down, but never out of the tile. */
        {
            int six = nstat > 4;
            int tile_y = r.y - (six ? (ZK_SLOT_Y6 - ZK_GRID_Y) : ZK_SLOT_DY4);
            int tile_h = six ? 102 : 160;
            TCHECK(slot->parent_y >= r.y, "%s y %d above spec %d", id, slot->parent_y, r.y);
            TCHECK(slot->parent_y + ZK_SLOT_H <= tile_y + tile_h - 2, "%s y %d leaves tile", id, slot->parent_y);
        }
        TEQ_I(slot->parent_w, ZK_SLOT_W);
        TEQ_I(slot->parent_h, ZK_SLOT_H);
        TCHECK(slot->w <= ZK_SLOT_W, "%s label width %d", id, slot->w);
    }
}

static void test_running_long(void)
{
    zk_ui_label_box_t b[96];
    int n;
    int i;
    char cur[128];
    const zk_ui_label_box_t *label;
    const zk_ui_label_box_t *head;
    const zk_ui_label_box_t *time;
    const zk_ui_label_box_t *sub;
    int saw = 0;
    TCHECK(load_fix("running-long", ZK_SCREEN_HOME) == 0, "load running-long");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, "running-long", 1);
    make_title(cur, 2);
    label = find_box(b, n, "hero_label");
    head = find_box(b, n, "hero_headline");
    time = find_box(b, n, "hero_time");
    sub = find_box(b, n, "hero_secondary");
    TCHECK(label && head && time && sub, "running labels");
    if (label) {
        TEQ_S(label->text, "WATERING NOW " ZK_MID " MORNING CYCLE");
    }
    if (head) {
        lv_point_t sz;
        char flat[160];
        strip_breaks(flat, sizeof flat, head->text);
        lv_text_get_size(&sz, cur, &zk_font_lit_48, 0, 0, 332, LV_TEXT_FLAG_NONE);
        TEQ_I(head->w, 332);
        TEQ_I(head->x - head->parent_x, 24);
        if ((int)sz.y > 52 * 3) {
            TCHECK(prefix_ellipsis(head->text, cur), "headline %s", head->text);
        } else {
            TEQ_S(flat, cur);
            TCHECK(!ends_ellipsis(head->text), "headline ellipsized");
        }
    }
    if (time) {
        TCHECK(time->visible, "time hidden");
        TEQ_S(time->text, "3 min left");
    }
    if (sub && sub->visible) {
        TCHECK(strstr(sub->text, "next") != NULL, "secondary %s", sub->text);
    } else {
        const zk_ui_label_box_t *rain = find_box(b, n, "rain_text");
        /* Rain yields before the ends line. A line that still does not fit may go. */
        TCHECK(rain && !rain->visible, "secondary dropped while rain was still showing");
    }
    for (i = 0; i < 4; i++) {
        char id[32];
        const zk_ui_label_box_t *t;
        snprintf(id, sizeof id, "tile_title%d", i);
        t = find_box(b, n, id);
        if (t && prefix_ellipsis(t->text, cur)) {
            saw = 1;
        }
    }
    TCHECK(saw, "running tile name not ellipsized");
    check_stop_gap(b, n);
}

static void snapshot_geom(const zk_ui_label_box_t *b, int n, const char *id, int *x, int *y, int *w, int *h)
{
    const zk_ui_label_box_t *t = find_box(b, n, id);
    TCHECK(t != NULL, "missing %s", id);
    if (!t) {
        *x = *y = *w = *h = -1;
        return;
    }
    *x = t->x;
    *y = t->y;
    *w = t->w;
    *h = t->h;
}

/* Soil on and soil off keep the same title and slot boxes. */
static void test_home_title_width(void)
{
    zk_ui_label_box_t rain[96];
    zk_ui_label_box_t dry[96];
    int nr;
    int nd;
    int i;
    /* The slot row stays 24 px. A soil bar only insets the words inside that row. */
    const char *ids[] = {"tile_title0", "tile_title1", "tile_title2", "tile_title3", "hero_label",
                         "hero_headline", "pause_word", "stop_word", "rain_text", NULL};
    TCHECK(load_fix("home-rain", ZK_SCREEN_HOME) == 0, "load home-rain");
    nr = grab(rain, 96);
    if (nr < 0) {
        return;
    }
    assert_rain_end(rain, nr, "home-rain");
    {
        const zk_ui_label_box_t *rt = find_box(rain, nr, "rain_text");
        TCHECK(rt && rt->visible, "home-rain meter");
        if (rt) {
            TEQ_S(rt->text, "Rain 0.2 in " ZK_MID " last 72 h");
            TCHECK(strstr(rt->text, "0.0") == NULL, "rain line said 0.0");
            TCHECK(strstr(rt->text, "0.24") == NULL, "rain line kept the raw hundredths");
        }
    }
    TCHECK(load_fix("home-nosoil", ZK_SCREEN_HOME) == 0, "load home-nosoil");
    nd = grab(dry, 96);
    if (nd < 0) {
        return;
    }
    for (i = 0; i < 4; i++) {
        char id[32];
        char want[32];
        const zk_ui_label_box_t *t;
        zk_ui_tile_dbg_t tile;
        snprintf(id, sizeof id, "tile_title%d", i);
        t = find_box(dry, nd, id);
        TCHECK(t != NULL, "nosoil missing %s", id);
        if (t) {
            TEQ_I(t->w, t->parent_w - 28);
            snprintf(want, sizeof want, "Test Station %d", i + 1);
            TEQ_S(t->text, want);
        }
        t = find_box(rain, nr, id);
        if (t) {
            snprintf(want, sizeof want, "Test Station %d", i + 1);
            TEQ_S(t->text, want);
        }
        TCHECK(zk_ui_debug_tile(i, &tile) == 1, "tile %d", i);
        TEQ_I(tile.drop_visible, 0);
        TEQ_I(tile.bg, 0xFCF5E6);
        TEQ_I(tile.title_w, tile.tile_w - 28);
        TEQ_I(tile.border_w, 2);
    }
    for (i = 0; ids[i]; i++) {
        int x1, y1, w1, h1, x2, y2, w2, h2;
        snapshot_geom(rain, nr, ids[i], &x1, &y1, &w1, &h1);
        snapshot_geom(dry, nd, ids[i], &x2, &y2, &w2, &h2);
        TCHECK(x1 == x2 && y1 == y2 && w1 == w2 && h1 == h2, "%s soil rect %d,%d %dx%d vs %d,%d %dx%d", ids[i], x1,
               y1, w1, h1, x2, y2, w2, h2);
    }
}

static void test_sheets_soil_offline(void)
{
    zk_ui_label_box_t b[96];
    int n;
    int i;
    const zk_ui_label_box_t *title;
    const zk_ui_label_box_t *slot;
    const zk_ui_label_box_t *head;
    const zk_ui_label_box_t *stop;
    TCHECK(load_fix("home-rain", ZK_SCREEN_HOME) == 0, "load home for run sheet");
    zk_ui_debug_set_view(ZK_SCREEN_SHEET_RUN, 0, 0);
    zk_ui_refresh();
    n = grab(b, 96);
    title = find_box(b, n, "sheet_title");
    TCHECK(title != NULL, "run sheet title");
    if (title) {
        TCHECK(strstr(title->text, "Run ") != NULL, "run title %s", title->text);
        TCHECK(strstr(title->text, "min?") != NULL, "run title minutes %s", title->text);
    }
    TCHECK(load_fix("home-stale", ZK_SCREEN_HOME) == 0, "load home-stale");
    n = grab(b, 96);
    for (i = 0; i < 4; i++) {
        char id[32];
        snprintf(id, sizeof id, "tile_slot%d", i);
        slot = find_box(b, n, id);
        TCHECK(slot != NULL, "stale slot %d", i);
        if (slot) {
            int tw;
            TEQ_S(slot->text, "Rate not measured");
            TCHECK(strstr(slot->text, "0%") == NULL, "stale showed a percent");
            tw = (int)lv_text_get_width(slot->text, (uint32_t)strlen(slot->text), &zk_font_m_18, 0);
            TCHECK(tw <= ZK_SLOT_W, "rate width %d", tw);
            TCHECK(tw <= slot->w, "rate clipped %d in box %d", tw, slot->w);
        }
    }
    assert_slots_fixed(b, n, 4);
    TCHECK(load_fix("home-rain", ZK_SCREEN_HOME) == 0, "load needs-update");
    zk_data_debug_force_needs_update(1);
    zk_ui_debug_set_view(ZK_SCREEN_HOME, 0, -1);
    zk_ui_refresh();
    n = grab(b, 96);
    head = find_box(b, n, "hero_headline");
    stop = find_box(b, n, "stop_word");
    TCHECK(head && stop, "offline labels");
    if (head) {
        TEQ_S(head->text, "Can't reach controller");
    }
    if (stop) {
        TEQ_S(stop->text, "STOP");
        TCHECK(stop->visible, "STOP hidden while offline");
    }
    zk_data_debug_force_needs_update(0);
}

static void test_hero_longname(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *head;
    const zk_ui_label_box_t *time;
    const zk_ui_label_box_t *sub;
    const zk_ui_label_box_t *rain;
    const char *full = "North Orchard Drip Line East Bed Zone";
    char flat[160];
    /* Rain yields before the ends/next line. The headline box stays 22/332. */
    TCHECK(load_fix("dc-watering-4-longname", ZK_SCREEN_HOME) == 0, "load longname");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    check_geometry(b, n, "longname", 0);
    head = find_box(b, n, "hero_headline");
    time = find_box(b, n, "hero_time");
    sub = find_box(b, n, "hero_secondary");
    rain = find_box(b, n, "rain_text");
    TCHECK(head && time && sub && rain, "longname labels");
    if (head) {
        lv_point_t sz;
        strip_breaks(flat, sizeof flat, head->text);
        lv_text_get_size(&sz, full, &zk_font_lit_48, 0, 0, 332, LV_TEXT_FLAG_NONE);
        TEQ_I(head->w, 332);
        TEQ_I(head->x - head->parent_x, 24);
        if ((int)sz.y > 52 * 3) {
            TCHECK(wrap_prefix(head->text, full), "long headline %s", head->text);
            TCHECK(head->h <= 52 * 3 + 16, "headline box grew past 3 lines (%d)", head->h);
        } else {
            TEQ_S(flat, full);
            TCHECK(!ends_ellipsis(head->text), "long headline ellipsized: %s", head->text);
        }
    }
    if (time) {
        TCHECK(time->visible, "time hidden");
        TCHECK(strstr(time->text, "min left") != NULL, "time %s", time->text);
    }
    if (sub) {
        TCHECK(sub->visible, "ends/next line dropped");
        TCHECK(strstr(sub->text, "next") != NULL, "secondary %s", sub->text);
    }
    if (rain) {
        TCHECK(!rain->visible, "rain stayed in front of the ends line");
    }
}

static void test_running_sheet(void)
{
    zk_ui_label_box_t b[96];
    int n;
    int i;
    const zk_ui_label_box_t *title;
    const zk_ui_label_box_t *label;
    const zk_ui_label_box_t *body;
    const zk_ui_label_box_t *timed;
    const zk_ui_label_box_t *ok;
    const zk_ui_label_box_t *stop;
    TCHECK(load_fix("dc-watering-6", ZK_SCREEN_HOME) == 0, "load dc-watering-6");
    zk_ui_debug_set_view(ZK_SCREEN_SHEET_STOP_STATION, 0, 2);
    zk_ui_refresh();
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    title = find_box(b, n, "sheet_title");
    label = find_box(b, n, "sheet_label");
    body = find_box(b, n, "sheet_body");
    timed = find_box(b, n, "sheet_time");
    stop = find_box(b, n, "stop_word");
    TCHECK(title && label && body && timed && stop, "running sheet labels");
    if (title) {
        TEQ_S(title->text, "Front Yard Drip Emitters is watering");
        TCHECK(title->visible, "running title hidden");
        TEQ_I(title->y, 62);
    }
    if (label) {
        TEQ_S(label->text, "WATERING NOW " ZK_MID " LAWN A + B " ZK_MID " WEEKDAYS AM");
        TEQ_I(label->y, 34);
    }
    if (timed) {
        TEQ_S(timed->text, "2 min left " ZK_MID " ends 6:28" ZK_NBSP "AM");
        TCHECK(timed->visible, "time line hidden");
        TEQ_I(timed->y, 150);
    }
    if (body) {
        TEQ_S(body->text, "Next: Beds. Tap STOP below to end all watering.");
        TEQ_I(body->y, 196);
    }
    if (stop) {
        TEQ_S(stop->text, "STOP");
        TCHECK(stop->visible, "STOP hidden on the running sheet");
    }
    ok = NULL;
    for (i = 0; i < n; i++) {
        if (!b[i].visible) {
            continue;
        }
        TCHECK(strcmp(b[i].text, "Stop station") != 0, "Stop station still drawn");
        TCHECK(strcmp(b[i].text, "Cancel") != 0, "Cancel still drawn on the running sheet");
        TCHECK(strstr(b[i].text, "Stop ") != b[i].text, "stop-station title still drawn: %s", b[i].text);
        if (strcmp(b[i].text, "OK") == 0) {
            ok = &b[i];
        }
    }
    TCHECK(ok != NULL, "running sheet OK");
    if (ok) {
        TCHECK(ok->visible, "OK hidden");
        TEQ_I(ok->parent_x, 36);
        TEQ_I(ok->parent_y, 232);
        TEQ_I(ok->parent_w, 352);
        TEQ_I(ok->parent_h, 96);
    }
}

static void parent_box(const zk_ui_label_box_t *b, int n, const char *id, int *x, int *y, int *w, int *h)
{
    const zk_ui_label_box_t *t = find_box(b, n, id);
    TCHECK(t != NULL, "missing %s", id);
    if (!t) {
        *x = *y = *w = *h = -1;
        return;
    }
    *x = t->parent_x;
    *y = t->parent_y;
    *w = t->parent_w;
    *h = t->parent_h;
}

static void expect_parent(const zk_ui_label_box_t *b, int n, const char *id, int x, int y, int w, int h)
{
    int ax, ay, aw, ah;
    parent_box(b, n, id, &ax, &ay, &aw, &ah);
    TCHECK(ax == x && ay == y && aw == w && ah == h, "%s parent %d,%d %dx%d want %d,%d %dx%d", id, ax, ay, aw, ah, x,
           y, w, h);
}

static void test_schedules_screen(void)
{
    zk_ui_label_box_t four[96];
    zk_ui_label_box_t six[96];
    zk_ui_label_box_t rain[96];
    zk_ui_label_box_t dry[96];
    zk_ui_label_box_t home[96];
    int n4;
    int n6;
    int nr;
    int nd;
    int nh;
    int i;
    const char *ids[] = {"sched_word", "pause_word", "stop_word", "sched_label", "next_word", NULL};
    const zk_ui_label_box_t *t;
    const zk_ui_label_box_t *hero;
    TCHECK(load_fix("dc-schedules-4", ZK_SCREEN_HOME) == 0, "load schedules home");
    nh = grab(home, 96);
    if (nh < 0) {
        return;
    }
    t = find_box(home, nh, "sched_word");
    TCHECK(t && t->visible, "home Schedules word");
    if (t) {
        TEQ_S(t->text, "Schedules");
    }
    t = find_box(home, nh, "pause_word");
    if (t) {
        TEQ_S(t->text, "Pause for rain");
    }
    expect_parent(home, nh, "sched_word", 16, 360, 120, 104);
    expect_parent(home, nh, "pause_word", 148, 360, 244, 104);
    expect_parent(home, nh, "stop_word", 404, 360, 380, 104);

    TCHECK(load_fix("dc-schedules-4", ZK_SCREEN_SCHEDULES) == 0, "load schedules 4");
    n4 = grab(four, 96);
    if (n4 < 0) {
        return;
    }
    t = find_box(four, n4, "sched_word");
    if (t) {
        TEQ_S(t->text, "Home");
        TCHECK(t->visible, "Home hidden");
    }
    t = find_box(four, n4, "sched_label");
    if (t) {
        TEQ_S(t->text, "SCHEDULES " ZK_MID " READ ONLY");
        TCHECK(t->visible, "schedules label hidden");
    }
    t = find_box(four, n4, "sched_name0");
    if (t) {
        TEQ_S(t->text, "Lawn " ZK_MID " Morning");
        TCHECK(t->visible, "first program hidden");
    }
    t = find_box(four, n4, "sched_meta0");
    if (t) {
        TEQ_S(t->text, "Every day " ZK_MID " 6:10" ZK_NBSP "AM " ZK_MID " 3 steps " ZK_MID " 18 min " ZK_MID " Enabled");
    }
    t = find_box(four, n4, "sched_name1");
    if (t) {
        TEQ_S(t->text, "Beds " ZK_MID " Every 2 days");
        TCHECK(t->visible, "second program hidden");
    }
    t = find_box(four, n4, "sched_meta1");
    if (t) {
        TEQ_S(t->text,
              "Mon Wed Fri " ZK_MID " 7:00" ZK_NBSP "AM " ZK_MID " 2 steps " ZK_MID " 12 min " ZK_MID " Disabled");
    }
    t = find_box(four, n4, "sched_note");
    if (t) {
        TEQ_S(t->text, ZK_SCHED_NOTE);
        TCHECK(t->visible, "fix-on-phone note hidden");
    }
    t = find_box(four, n4, "next_word");
    if (t) {
        TCHECK(!t->visible, "Next shown for two programs");
    }
    t = find_box(four, n4, "stop_word");
    if (t) {
        TEQ_S(t->text, "STOP");
        TCHECK(t->visible, "STOP hidden on schedules");
    }
    hero = find_box(four, n4, "hero_headline");
    if (hero) {
        TCHECK(!hero->visible, "hero still showing on schedules");
    }
    expect_parent(four, n4, "sched_word", 16, 360, 120, 104);
    expect_parent(four, n4, "pause_word", 148, 360, 244, 104);
    expect_parent(four, n4, "stop_word", 404, 360, 380, 104);
    expect_parent(four, n4, "sched_label", 16, 16, 768, 332);
    expect_parent(four, n4, "next_word", 400, 232, 364, 96);

    TCHECK(load_fix("dc-schedules-6", ZK_SCREEN_SCHEDULES) == 0, "load schedules 6");
    n6 = grab(six, 96);
    if (n6 < 0) {
        return;
    }
    for (i = 0; ids[i]; i++) {
        int x1, y1, w1, h1, x2, y2, w2, h2;
        parent_box(four, n4, ids[i], &x1, &y1, &w1, &h1);
        parent_box(six, n6, ids[i], &x2, &y2, &w2, &h2);
        TCHECK(x1 == x2 && y1 == y2 && w1 == w2 && h1 == h2, "%s 4-up vs 6-up %d,%d %dx%d vs %d,%d %dx%d", ids[i], x1,
               y1, w1, h1, x2, y2, w2, h2);
    }

    TCHECK(load_fix("home-rain", ZK_SCREEN_SCHEDULES) == 0, "load rain schedules");
    nr = grab(rain, 96);
    if (nr < 0) {
        return;
    }
    TCHECK(load_fix("home-nosoil", ZK_SCREEN_SCHEDULES) == 0, "load nosoil schedules");
    nd = grab(dry, 96);
    if (nd < 0) {
        return;
    }
    for (i = 0; ids[i]; i++) {
        int x1, y1, w1, h1, x2, y2, w2, h2;
        parent_box(rain, nr, ids[i], &x1, &y1, &w1, &h1);
        parent_box(dry, nd, ids[i], &x2, &y2, &w2, &h2);
        TCHECK(x1 == x2 && y1 == y2 && w1 == w2 && h1 == h2, "%s soil rect %d,%d %dx%d vs %d,%d %dx%d", ids[i], x1, y1,
               w1, h1, x2, y2, w2, h2);
    }

    TCHECK(load_fix("dc-schedules-1", ZK_SCREEN_SCHEDULES) == 0, "load schedules 1");
    n4 = grab(four, 96);
    t = find_box(four, n4, "sched_label");
    if (t) {
        TEQ_S(t->text, "SCHEDULES " ZK_MID " READ ONLY");
    }
    t = find_box(four, n4, "next_word");
    if (t) {
        TCHECK(!t->visible, "Next shown for one program");
    }
    t = find_box(four, n4, "sched_name1");
    if (t) {
        TCHECK(!t->visible, "second row on a one-program list");
    }

    TCHECK(load_fix("dc-schedules-3", ZK_SCREEN_SCHEDULES) == 0, "load schedules 3");
    n4 = grab(four, 96);
    t = find_box(four, n4, "sched_label");
    if (t) {
        TEQ_S(t->text, "SCHEDULES " ZK_MID " READ ONLY " ZK_MID " 1" ZK_EN "2 of 3");
    }
    t = find_box(four, n4, "next_word");
    if (t) {
        TCHECK(t->visible, "Next hidden when N is 3");
    }
    t = find_box(four, n4, "sched_name0");
    if (t) {
        TEQ_S(t->text, "Lawn " ZK_MID " Morning");
    }
    zk_ui_debug_set_sched(2);
    zk_ui_refresh();
    n4 = grab(four, 96);
    t = find_box(four, n4, "sched_label");
    if (t) {
        TEQ_S(t->text, "SCHEDULES " ZK_MID " READ ONLY " ZK_MID " 3 of 3");
    }
    t = find_box(four, n4, "sched_name0");
    if (t) {
        TEQ_S(t->text, "Trees " ZK_MID " Sundays");
        TCHECK(t->visible, "page 2 name hidden");
    }
    t = find_box(four, n4, "sched_name1");
    if (t) {
        TCHECK(!t->visible, "page 2 drew a second row");
    }
    t = find_box(four, n4, "next_word");
    if (t) {
        TCHECK(t->visible, "Next hidden on the last page");
    }
}

/* Regression: names that fit the spec clamp (3 lines for 4 stations, 2 for 6)
 * must not be ellipsized because the soil slot is below them. */
static void test_tile_names_fit_clamp(void)
{
    static const struct { const char *fix; int nstat; } cases[] = {{"dc-idle-4", 4}, {"dc-soil-4", 4}, {"dc-idle-6", 6}, {"dc-soil-6", 6}};
    size_t k;
    for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        zk_ui_label_box_t b[96];
        int n;
        int i;
        TCHECK(load_fix(cases[k].fix, ZK_SCREEN_HOME) == 0, "load %s", cases[k].fix);
        n = grab(b, 96);
        for (i = 0; i < cases[k].nstat; i++) {
            char id[32];
            const zk_ui_label_box_t *t;
            snprintf(id, sizeof id, "tile_title%d", i);
            t = find_box(b, n, id);
            if (!t || !t->visible) {
                continue;
            }
            TCHECK(!t->clamped, "%s %s clamped: %s", cases[k].fix, id, t->text);
            TCHECK(strstr(t->text, "\xE2\x80\xA6") == NULL, "%s %s ellipsis: %s", cases[k].fix, id, t->text);
        }
        assert_slots_fixed(b, n, cases[k].nstat);
    }
}

static void expect_btn(const zk_ui_label_box_t *b, int n, const char *id, int x, int y, int w, int h)
{
    const zk_ui_label_box_t *t = find_box(b, n, id);
    TCHECK(t && t->visible, "%s not drawn", id);
    if (!t) {
        return;
    }
    TCHECK(t->parent_x == x && t->parent_y == y && t->parent_w == w && t->parent_h == h,
           "%s parent %d,%d %dx%d want %d,%d %dx%d", id, t->parent_x, t->parent_y, t->parent_w, t->parent_h, x, y, w,
           h);
}

/* Every drawn sheet button's outer rect is its layout hit rect. */
static void test_button_rects(void)
{
    zk_ui_label_box_t b[96];
    zk_ui_glyph_dbg_t minus;
    zk_ui_glyph_dbg_t plus_h;
    zk_ui_glyph_dbg_t plus_v;
    int n;
    const zk_ui_label_box_t *word;
    TCHECK(load_fix("dc-idle-6", ZK_SCREEN_HOME) == 0, "load run sheet");
    zk_ui_debug_set_view(ZK_SCREEN_SHEET_RUN, 0, 2);
    zk_ui_debug_set_minutes(1);
    zk_ui_refresh();
    n = grab(b, 96);
    /* Stepper words are hidden; their parent is still the 96 px button. */
    expect_parent(b, n, "minus_word", 36, 232, 96, 96);
    expect_parent(b, n, "plus_word", 288, 232, 96, 96);
    expect_btn(b, n, "cancel_word", 408, 232, 168, 96);
    expect_btn(b, n, "run_word", 588, 232, 176, 96);
    word = find_box(b, n, "minus_word");
    TCHECK(word && !word->visible, "minus word still shown");
    word = find_box(b, n, "plus_word");
    TCHECK(word && !word->visible, "plus word still shown");
    TCHECK(zk_ui_debug_glyphs(&minus, &plus_h, &plus_v) == 1, "glyphs");
    TEQ_I(minus.present, 1);
    TEQ_I(minus.x, 75);
    TEQ_I(minus.y, 278);
    TEQ_I(minus.w, 18);
    TEQ_I(minus.h, 4);
    TEQ_I((int)minus.color, 0x6E5A45);
    TEQ_I(plus_h.present, 1);
    TEQ_I(plus_h.x, 327);
    TEQ_I(plus_h.y, 278);
    TEQ_I(plus_h.w, 18);
    TEQ_I(plus_h.h, 4);
    TEQ_I((int)plus_h.color, 0x4A2F12);
    TEQ_I(plus_v.present, 1);
    TEQ_I(plus_v.x, 334);
    TEQ_I(plus_v.y, 271);
    TEQ_I(plus_v.w, 4);
    TEQ_I(plus_v.h, 18);
    TEQ_I((int)plus_v.color, 0x4A2F12);

    zk_ui_debug_set_view(ZK_SCREEN_SHEET_STOP_ALL, 0, -1);
    zk_ui_refresh();
    n = grab(b, 96);
    expect_btn(b, n, "cancel_word", 40, 328, 352, 112);
    expect_btn(b, n, "stop_all_word", 408, 328, 352, 112);

    TCHECK(load_fix("dc-overlap-6", ZK_SCREEN_HOME) == 0, "load overlap sheet");
    zk_ui_debug_set_view(ZK_SCREEN_SHEET_OVERLAP, 0, -1);
    zk_ui_refresh();
    n = grab(b, 96);
    expect_btn(b, n, "ok_word", 524, 232, 240, 96);
}

/* A 24-character station name stays whole. A longer name clamps at 3 lines. */
static void test_hero_name_24(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *head;
    const char *full = "Front Yard Drip Emitters";
    char flat[160];
    TCHECK(load_fix("dc-watering-4", ZK_SCREEN_HOME) == 0, "load 24-char name");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    head = find_box(b, n, "hero_headline");
    TCHECK(head != NULL, "24-char headline");
    TEQ_I((int)strlen(full), 24);
    if (!head) {
        return;
    }
    strip_breaks(flat, sizeof flat, head->text);
    TEQ_S(flat, full);
    TCHECK(!ends_ellipsis(head->text), "24-char name ellipsized: %s", head->text);
    TCHECK(strstr(head->text, "\xE2\x80\xA6") == NULL, "24-char name clamped: %s", head->text);
}

static void test_pending_headline(void)
{
    zk_ui_label_box_t b[96];
    int n;
    int w;
    const zk_ui_label_box_t *head;
    const zk_ui_label_box_t *label;
    const zk_ui_label_box_t *sub;
    const char *line2 = "confirmed" ZK_NBSP "yet";
    TCHECK(load_fix("dc-watering-4", ZK_SCREEN_HOME) == 0, "load pending hero");
    zk_ui_debug_set_stop(ZK_STOP_QUEUED);
    zk_ui_refresh();
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    head = find_box(b, n, "hero_headline");
    label = find_box(b, n, "hero_label");
    sub = find_box(b, n, "hero_secondary");
    TCHECK(head && label && sub, "reachable pending labels");
    if (head) {
        TEQ_S(head->text, ZK_STOP_PENDING_HEADLINE);
        TCHECK(strstr(head->text, "\n") == head->text + strlen("Stop not"), "headline break");
    }
    if (label) {
        TEQ_S(label->text, ZK_STOP_PENDING_LABEL);
        TCHECK(strstr(label->text, "NO REPLY") == NULL, "rendered no-reply label");
    }
    if (sub) {
        TEQ_S(sub->text, ZK_STOP_PENDING_RETRY);
    }
    w = (int)lv_text_get_width(line2, (uint32_t)strlen(line2), &zk_font_lit_48, 0);
    /* This font is 334 px. The headline box grows just enough to keep the line. */
    TCHECK(w > 300 && w <= 348, "confirmed yet is %d px", w);
    if (head) {
        TCHECK(head->w >= w, "headline box %d clips line 2 (%d)", head->w, w);
        TCHECK(head->x + head->w <= head->parent_x + head->parent_w, "headline leaves the hero");
    }
    w = (int)lv_text_get_width(ZK_STOP_PENDING_RETRY, (uint32_t)strlen(ZK_STOP_PENDING_RETRY), &zk_font_r_26, 0);
    TCHECK(w > 0 && w <= 332, "reachable line 2 is %d px", w);
    w = (int)lv_text_get_width(ZK_STOP_PENDING_OFFLINE, (uint32_t)strlen(ZK_STOP_PENDING_OFFLINE), &zk_font_r_26, 0);
    TCHECK(w > 0 && w <= 332, "unreachable line 2 is %d px", w);

    zk_data_debug_force_stale(1);
    zk_ui_debug_set_stop(ZK_STOP_WAIT);
    zk_ui_refresh();
    n = grab(b, 96);
    head = find_box(b, n, "hero_headline");
    label = find_box(b, n, "hero_label");
    sub = find_box(b, n, "hero_secondary");
    TCHECK(head && label && sub, "unreachable pending labels");
    if (head) {
        TEQ_S(head->text, ZK_STOP_PENDING_HEADLINE);
        TCHECK(strstr(head->text, "sent yet") == NULL, "rendered sent-yet headline");
    }
    if (label) {
        TEQ_S(label->text, "STOP " ZK_MID " LAST UPDATE 6:26" ZK_NBSP "AM");
    }
    if (sub) {
        TEQ_S(sub->text, ZK_STOP_PENDING_OFFLINE);
    }
    zk_data_debug_force_stale(0);
    zk_ui_debug_set_stop(-1);
}

static void test_schedules_empty_offline(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *t;
    TCHECK(load_fix("dc-schedules-empty", ZK_SCREEN_SCHEDULES) == 0, "load empty schedules");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    t = find_box(b, n, "sched_label");
    if (t) {
        TEQ_S(t->text, "SCHEDULES " ZK_MID " READ ONLY");
    }
    t = find_box(b, n, "sched_empty");
    TCHECK(t && t->visible, "empty title");
    if (t) {
        TEQ_S(t->text, ZK_SCHED_EMPTY);
    }
    t = find_box(b, n, "sched_empty_body");
    TCHECK(t && t->visible, "empty body");
    if (t) {
        TEQ_S(t->text, ZK_SCHED_EMPTY_BODY);
    }
    t = find_box(b, n, "sched_name0");
    if (t) {
        TCHECK(!t->visible, "empty list drew a program");
    }
    t = find_box(b, n, "next_word");
    if (t) {
        TCHECK(!t->visible, "Next shown on an empty list");
    }

    TCHECK(load_fix("dc-schedules-offline", ZK_SCREEN_SCHEDULES) == 0, "load offline schedules");
    zk_data_debug_force_stale(1);
    zk_ui_refresh();
    n = grab(b, 96);
    t = find_box(b, n, "sched_label");
    TCHECK(t && t->visible, "offline schedules label");
    if (t) {
        TEQ_S(t->text, "SCHEDULES " ZK_MID " LAST UPDATE 6:42" ZK_NBSP "AM");
    }
    t = find_box(b, n, "sched_name0");
    if (t) {
        TEQ_S(t->text, "Lawn " ZK_MID " Morning");
        TCHECK(t->visible, "offline program hidden");
    }
    zk_data_debug_force_stale(0);
}

static void test_unavailable_names(void)
{
    int i;
    TCHECK(load_fix("dc-idle-6", ZK_SCREEN_HOME) == 0, "load idle names");
    for (i = 0; i < 6; i++) {
        zk_ui_tile_dbg_t tile;
        TCHECK(zk_ui_debug_tile(i, &tile) == 1, "idle tile %d", i);
        TEQ_I((int)tile.name_rgb, 0x4A2F12);
    }
    TCHECK(load_fix("dc-offline-6", ZK_SCREEN_HOME) == 0, "load offline names");
    zk_data_debug_force_stale(1);
    zk_ui_refresh();
    for (i = 0; i < 6; i++) {
        zk_ui_tile_dbg_t tile;
        TCHECK(zk_ui_debug_tile(i, &tile) == 1, "offline tile %d", i);
        TEQ_I((int)tile.name_rgb, 0x6E5A45);
    }
    zk_data_debug_force_stale(0);
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
    test_tile_names_fit_clamp();
    test_hero_longname();
    test_hero_name_24();
    test_pending_headline();
    test_home_title_width();
    test_running_long();
    test_sheets_soil_offline();
    test_running_sheet();
    test_schedules_screen();
    test_schedules_empty_offline();
    test_button_rects();
    test_unavailable_names();
    zk_data_stop();
    lv_deinit();
    rc = zk_test_report("test_ui_text");
    return rc;
}
