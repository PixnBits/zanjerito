#include "zk_test.h"

#include "data.h"
#include "platform.h"
#include "ui.h"
#include "zk_copy.h"
#include "zk_flow.h"
#include "zk_layout.h"

#include "generated/zk_fonts.h"

#include <dirent.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

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
        TEQ_I(tile.title_w, tile.tile_w - 20);
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
        /* Word wrap replaces the space after the middot with a newline. */
        TCHECK(strncmp(label->text, "WATERING NOW " ZK_MID, 15) == 0, "prefix %s", label->text);
        TCHECK(label->text[15] == ' ' || label->text[15] == '\n', "prefix break %s", label->text);
        /* A wrapped label replaces the space with a newline, so the flat prefix check drops it. */
        TCHECK(strcmp(label->text, full) == 0 || prefix_ellipsis(label->text, full) || wrap_prefix(label->text, full),
               "label %s", label->text);
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
        TEQ_I(tile.title_w, tile.tile_w - 20);
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
            TEQ_I(t->w, t->parent_w - 20);
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
        TEQ_I(tile.title_w, tile.tile_w - 20);
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
        static const lv_font_t *faces[] = {&zk_font_lit_48, &zk_font_lit_40, &zk_font_lit_32};
        int fits = 0;
        int fi;
        strip_breaks(flat, sizeof flat, head->text);
        TEQ_I(head->w, 332);
        TEQ_I(head->x - head->parent_x, 24);
        for (fi = 0; fi < 3; fi++) {
            lv_point_t sz;
            int lh = (int)lv_font_get_line_height(faces[fi]);
            lv_text_get_size(&sz, full, faces[fi], 0, 0, 332, LV_TEXT_FLAG_NONE);
            if ((int)sz.y <= lh * 3) {
                fits = 1;
            }
        }
        if (fits) {
            TEQ_S(flat, full);
            TCHECK(!ends_ellipsis(head->text), "long headline ellipsized: %s", head->text);
            TCHECK(head->h <= (int)lv_font_get_line_height(head->font) * 3 + 16, "headline box %d", head->h);
        } else {
            TCHECK(wrap_prefix(head->text, full) || prefix_ellipsis(head->text, full), "long headline %s", head->text);
            TCHECK(head->h <= (int)lv_font_get_line_height(&zk_font_lit_32) * 3 + 16, "headline box grew (%d)", head->h);
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
        TEQ_I((int)stop->rgb, 0xFFFFFF);
    }
    {
        const zk_ui_label_box_t *pause = find_box(b, n, "pause_word");
        const zk_ui_label_box_t *sched = find_box(b, n, "sched_word");
        TCHECK(pause && pause->visible, "pause hidden on the running sheet");
        TCHECK(sched && sched->visible, "schedules hidden on the running sheet");
        if (pause) {
            TEQ_S(pause->text, ZK_WORD_PAUSE);
            TEQ_I((int)pause->rgb, 0x4A2F12);
        }
        if (sched) {
            TEQ_S(sched->text, ZK_WORD_SCHEDULES);
            TEQ_I((int)sched->rgb, 0x4A2F12);
        }
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

/* Regression: a name that fits the grid's line count (4-up up to 4 at the
 * floor, 6-up up to 3) must not be ellipsized because the soil slot is below it. */
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

/* Newlines stand in for a space, or, when allow_char is set, for a glyph break. */
static int text_covers(const char *shown, const char *full, int allow_char)
{
    const char *s = shown ? shown : "";
    const char *f = full ? full : "";
    while (*s || *f) {
        if (*s == '\n') {
            if (*f == ' ') {
                s++;
                f++;
                continue;
            }
            if (allow_char) {
                s++;
                continue;
            }
            return 0;
        }
        if (!*s || !*f || *s != *f) {
            return 0;
        }
        s++;
        f++;
    }
    return 1;
}

static int each_word_fits(const char *s, const lv_font_t *font, int width)
{
    const char *p = s ? s : "";
    if (!font || width < 1) {
        return 0;
    }
    while (*p) {
        const char *e = p;
        int w;
        while (*e && *e != ' ' && *e != '\n') {
            e++;
        }
        if (e > p) {
            w = (int)lv_text_get_width(p, (uint32_t)(e - p), font, 0);
            if (w > width + 1) {
                return 0;
            }
        }
        if (*e == ' ' || *e == '\n') {
            e++;
        }
        p = e;
    }
    return 1;
}

static int explicit_lines_fit(const char *s, const lv_font_t *font, int width)
{
    const char *p = s ? s : "";
    if (!font || width < 1) {
        return 0;
    }
    if (!*p) {
        return 1;
    }
    while (*p) {
        const char *nl = strchr(p, '\n');
        uint32_t n = nl ? (uint32_t)(nl - p) : (uint32_t)strlen(p);
        int w;
        if (n == 0) {
            return 0;
        }
        w = (int)lv_text_get_width(p, n, font, 0);
        if (w > width + 1) {
            return 0;
        }
        if (!nl) {
            return 1;
        }
        p = nl + 1;
    }
    return 1;
}

static int hang_lines_fit(const char *s, const zk_ui_label_box_t *ln, const zk_ui_label_box_t *rest)
{
    const char *nl;
    const char *p;
    int w;
    if (!s || !ln || !ln->font || !rest || !rest->font) {
        return 0;
    }
    nl = strchr(s, '\n');
    if (!nl) {
        return 0;
    }
    w = (int)lv_text_get_width(s, (uint32_t)(nl - s), ln->font, 0);
    if (w > ln->w + 1) {
        return 0;
    }
    p = nl + 1;
    while (*p) {
        const char *n2 = strchr(p, '\n');
        uint32_t n = n2 ? (uint32_t)(n2 - p) : (uint32_t)strlen(p);
        if (n == 0) {
            return 0;
        }
        w = (int)lv_text_get_width(p, n, rest->font, 0);
        if (w > rest->w + 1) {
            return 0;
        }
        if (!n2) {
            return 1;
        }
        p = n2 + 1;
    }
    return 0;
}

static void assert_box_clear(const zk_ui_label_box_t *box, const zk_ui_label_box_t *a, const zk_ui_label_box_t *b,
                             const char *what)
{
    lv_point_t sz;
    if (!box || box->w < 1 || box->h < 1 || !box->font) {
        TCHECK(0, "%s has no box", what);
        return;
    }
    TCHECK(inside_parent(box), "%s outside parent (%d,%d %dx%d) parent (%d,%d %dx%d)", what, box->x, box->y, box->w,
           box->h, box->parent_x, box->parent_y, box->parent_w, box->parent_h);
    lv_text_get_size(&sz, box->text[0] ? box->text : " ", box->font, 0, box->line_space, box->w, LV_TEXT_FLAG_NONE);
    TCHECK((int)sz.y <= box->h + 1, "%s glyphs %d taller than box %d [%s]", what, (int)sz.y, box->h, box->text);
    if (a && a->visible && a->w > 0 && a->h > 0) {
        TCHECK(!overlaps(box, a), "%s overlaps %s (%d,%d %dx%d) vs (%d,%d %dx%d)", what, a->id, box->x, box->y,
               box->w, box->h, a->x, a->y, a->w, a->h);
    }
    if (b && b->visible && b->w > 0 && b->h > 0) {
        TCHECK(!overlaps(box, b), "%s overlaps %s (%d,%d %dx%d) vs (%d,%d %dx%d)", what, b->id, box->x, box->y,
               box->w, box->h, b->x, b->y, b->w, b->h);
    }
}

static void assert_wrap(const zk_ui_label_box_t *box, const char *what)
{
    int ok;
    if (!box || !box->font || box->w < 1) {
        TCHECK(0, "%s wrap box", what);
        return;
    }
    /* A newline is a drawn break. Without one, every word has to fit the line. */
    if (strchr(box->text, '\n')) {
        ok = explicit_lines_fit(box->text, box->font, box->w);
    } else {
        ok = each_word_fits(box->text, box->font, box->w);
    }
    TCHECK(ok, "%s breaks mid-word or past the line [%s] w=%d", what, box->text, box->w);
}

static int load_abs(const char *path, int screen)
{
    zk_app_t app;
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

/* mode: 0 idle, 1 schedule watering, 2 manual. The focus name is station 0. */
static int write_scene(char *dir, size_t cap, const char *name, int nstat, int mode)
{
    char path[256];
    FILE *f;
    int i;
    if (!dir || cap < 32 || !name || nstat < 1 || nstat > 6) {
        return -1;
    }
    if (!mkdtemp(dir)) {
        return -1;
    }
    snprintf(path, sizeof path, "%s/kiosk.json", dir);
    f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f,
            "{\n"
            " \"now\": \"2026-10-19T18:00:00-06:00\",\n"
            " \"timezone\": \"America/Denver\",\n"
            " \"phase\": \"%s\",\n"
            " \"lockout\": false,\n"
            " \"last_error\": \"\",\n"
            " \"current_station\": \"%s\",\n"
            " \"stations_on\": [%s],\n",
            mode ? "StationOn" : "Idle", mode ? "s0" : "", mode ? "\"s0\"" : "");
    fprintf(f,
            " \"pause\": {\"paused\": false, \"until\": null, \"label\": \"\", \"reason\": \"\", \"source\": \"\", "
            "\"rain_inches\": 0.0, \"last_rain_at\": null},\n"
            " \"rain_strip\": {\"show\": false, \"inches\": 0.0, \"hours\": 72},\n"
            " \"rain\": {\"enabled\": true, \"unavailable\": false, \"total_24h_inches\": 0.0, "
            "\"total_72h_inches\": 0.0, \"have_totals\": false},\n"
            " \"next_run\": {\"schedule_id\": \"weekdays\", \"name\": \"Weekdays\", "
            "\"at\": \"2026-10-20T06:10:00-06:00\", \"ends_at\": \"2026-10-20T06:28:00-06:00\", "
            "\"total_min\": 18, \"skipped_by_pause\": false},\n"
            " \"next_effective_run\": {\"schedule_id\": \"weekdays\", \"name\": \"Weekdays\", "
            "\"at\": \"2026-10-20T06:10:00-06:00\", \"ends_at\": \"2026-10-20T06:28:00-06:00\", "
            "\"total_min\": 18, \"skipped_by_pause\": false},\n");
    if (!mode) {
        fprintf(f, " \"run\": null,\n");
    } else {
        fprintf(f,
                " \"run\": {\"kind\": \"%s\", \"program_id\": \"%s\", \"program\": \"%s\", "
                "\"started_at\": \"2026-10-19T18:00:00-06:00\", \"step_index\": 0, \"step_count\": 1, "
                "\"current_station\": \"s0\", \"next_station\": \"\", \"step_elapsed_sec\": 0, "
                "\"step_remaining_sec\": 120, \"run_remaining_sec\": 120, \"run_total_sec\": 120, \"steps\": [{"
                "\"station_id\": \"s0\", \"title\": \"%s\", \"planned_sec\": 120, \"elapsed_sec\": 0, "
                "\"remaining_sec\": 120, \"state\": \"active\"}]},\n",
                mode == 2 ? "manual" : "schedule", mode == 2 ? "manual" : "weekdays", mode == 2 ? "manual" : "Weekdays",
                name);
    }
    fprintf(f, " \"stations\": [\n");
    for (i = 0; i < nstat; i++) {
        const char *colors[] = {"red", "yellow", "blue", "green", "red", "yellow"};
        fprintf(f,
                "  {\"id\": \"s%d\", \"title\": \"%s\", \"color\": \"%s\", \"on\": %s, \"state\": \"%s\", "
                "\"rain_pause_exempt\": false, \"soil_percent\": null}%s\n",
                i, i == 0 ? name : "Beds", colors[i], (mode && i == 0) ? "true" : "false",
                (mode && i == 0) ? "running" : "idle", i + 1 < nstat ? "," : "");
    }
    fprintf(f,
            " ],\n"
            " \"soil\": {\"enabled\": false, \"et_known\": false, \"et_stale\": false, \"show_bars\": false, "
            "\"updated_at\": null}\n}\n");
    fclose(f);
    snprintf(path, sizeof path, "%s/history.json", dir);
    f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f, "{\"entries\":[\n");
    for (i = 0; i < nstat; i++) {
        fprintf(f,
                " {\"outcome\": \"completed\", \"started_at\": \"2026-10-13T06:10:00-06:00\", "
                "\"stations\": [{\"station_id\": \"s%d\"}]}%s\n",
                i, i + 1 < nstat ? "," : "");
    }
    fprintf(f, "]}\n");
    fclose(f);
    return 0;
}

static void drop_scene(const char *dir)
{
    char path[256];
    if (!dir || !dir[0]) {
        return;
    }
    snprintf(path, sizeof path, "%s/kiosk.json", dir);
    remove(path);
    snprintf(path, sizeof path, "%s/history.json", dir);
    remove(path);
    snprintf(path, sizeof path, "%s/schedules.json", dir);
    remove(path);
    rmdir(dir);
}

static uint32_t t_utf8_next(const char *s, uint32_t i)
{
    unsigned char c;
    if (!s || !s[i]) {
        return i;
    }
    c = (unsigned char)s[i];
    if (c < 0x80) {
        return i + 1;
    }
    if ((c & 0xE0) == 0xC0 && s[i + 1]) {
        return i + 2;
    }
    if ((c & 0xF0) == 0xE0 && s[i + 1] && s[i + 2]) {
        return i + 3;
    }
    if ((c & 0xF8) == 0xF0 && s[i + 1] && s[i + 2] && s[i + 3]) {
        return i + 4;
    }
    return i + 1;
}

/* Longest prefix that fits. Does not retreat to a space, matching wrap_fill_line. */
static uint32_t oracle_fill_line(const char *s, uint32_t nbytes, const lv_font_t *font, int max_w)
{
    uint32_t i = 0;
    uint32_t last_fit = 0;
    if (!s || nbytes == 0 || !font || max_w < 1) {
        return 0;
    }
    while (i < nbytes && s[i]) {
        uint32_t n;
        int w;
        if (s[i] == '\n') {
            return i;
        }
        n = t_utf8_next(s, i);
        if (n == i || n > nbytes) {
            break;
        }
        w = (int)lv_text_get_width(s, n, font, 0);
        if (w > max_w) {
            if (last_fit > 0) {
                return last_fit;
            }
            return n;
        }
        last_fit = n;
        i = n;
    }
    return i > 0 ? i : 0;
}

/* 1 when every word fits its line. Mirrors the tile/hero word wrap. */
static int oracle_words(const char *src, const lv_font_t *font, int first_w, int rest_w, int max_lines)
{
    const char *p;
    int nlines = 0;
    if (!src || !src[0]) {
        return 1;
    }
    if (!font || max_lines < 1 || first_w < 1) {
        return 0;
    }
    if (rest_w < 1) {
        rest_w = first_w;
    }
    p = src;
    while (*p) {
        int w = nlines == 0 ? first_w : rest_w;
        uint32_t i = 0;
        uint32_t last_space = 0;
        uint32_t nbytes = (uint32_t)strlen(p);
        int have_space = 0;
        uint32_t adv;
        if (nlines >= max_lines) {
            return 0;
        }
        while (i < nbytes && p[i] && p[i] != '\n') {
            uint32_t n = t_utf8_next(p, i);
            int lw;
            if (n <= i || n > nbytes) {
                return 0;
            }
            lw = (int)lv_text_get_width(p, n, font, 0);
            if (lw > w) {
                if (have_space && last_space > 0) {
                    break;
                }
                if (i > 0 && p[i] == ' ') {
                    break;
                }
                return 0;
            }
            if (p[n - 1] == ' ') {
                last_space = n;
                have_space = 1;
            }
            i = n;
        }
        adv = i;
        if (have_space && i < nbytes && p[i] && p[i] != '\n') {
            adv = last_space;
        }
        if (adv == 0) {
            return 0;
        }
        p += adv;
        while (*p == ' ') {
            p++;
        }
        if (*p == '\n') {
            p++;
            while (*p == ' ') {
                p++;
            }
        }
        nlines++;
    }
    return 1;
}

static int oracle_chars(const char *src, const lv_font_t *font, int first_w, int rest_w, int max_lines)
{
    const char *p = src ? src : "";
    int nlines = 0;
    if (!*p) {
        return 1;
    }
    if (!font || max_lines < 1 || first_w < 1) {
        return 0;
    }
    if (rest_w < 1) {
        rest_w = first_w;
    }
    while (*p) {
        int w = nlines == 0 ? first_w : rest_w;
        uint32_t adv;
        if (nlines >= max_lines || w < 1) {
            return 0;
        }
        adv = oracle_fill_line(p, (uint32_t)strlen(p), font, w);
        if (adv == 0) {
            return 0;
        }
        p += adv;
        while (*p == ' ') {
            p++;
        }
        if (*p == '\n') {
            p++;
            while (*p == ' ') {
                p++;
            }
        }
        nlines++;
    }
    return 1;
}

static int count_lines(const char *s)
{
    int n = 1;
    if (!s || !*s) {
        return 0;
    }
    for (; *s; s++) {
        if (*s == '\n') {
            n++;
        }
    }
    return n;
}

static int lines_fit_widths(const char *text, const lv_font_t *font, int first_w, int rest_w, int max_lines)
{
    const char *p = text ? text : "";
    int n = 0;
    if (!font) {
        return 0;
    }
    if (!*p) {
        return 1;
    }
    /* Equal-width word wraps stay in the original string. LVGL wraps them.
     * Measure that block, not the unwrapped line. */
    if (!strchr(p, '\n')) {
        lv_point_t sz;
        int lh = (int)lv_font_get_line_height(font);
        if (first_w < 1 || lh < 1 || max_lines < 1) {
            return 0;
        }
        lv_text_get_size(&sz, p, font, 0, 0, first_w, LV_TEXT_FLAG_NONE);
        return (int)sz.x <= first_w + 1 && (int)sz.y <= lh * max_lines;
    }
    while (*p) {
        const char *nl = strchr(p, '\n');
        uint32_t len = nl ? (uint32_t)(nl - p) : (uint32_t)strlen(p);
        int limit = n == 0 ? first_w : rest_w;
        int tw;
        if (n >= max_lines) {
            return 0;
        }
        tw = (int)lv_text_get_width(p, len, font, 0);
        if (tw > limit + 1) {
            return 0;
        }
        n++;
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    return 1;
}

/* Ellipsis ends the shown text. The cut is a word boundary, or inside one word wider than line_w. */
static int ellipsis_rule(const char *shown, const char *full, const lv_font_t *font, int line_w)
{
    const char *s = shown ? shown : "";
    const char *f = full ? full : "";
    const char *ell = "\xE2\x80\xA6";
    while (*s) {
        if (memcmp(s, ell, 3) == 0) {
            if (s[3] != 0) {
                return 0;
            }
            break;
        }
        if (*s == '\n') {
            if (*f == ' ') {
                f++;
            }
            s++;
            continue;
        }
        if (!*f || *s != *f) {
            return 0;
        }
        s++;
        f++;
    }
    if (memcmp(s, ell, 3) != 0) {
        return 0;
    }
    if (f == full || *f == '\0' || *f == ' ' || (f > full && f[-1] == ' ')) {
        return 1;
    }
    {
        const char *ws = f;
        const char *we;
        int w;
        while (ws > full && ws[-1] != ' ') {
            ws--;
        }
        we = f;
        while (*we && *we != ' ') {
            we++;
        }
        w = (int)lv_text_get_width(ws, (uint32_t)(we - ws), font, 0);
        return w > line_w;
    }
}

/* A newline inside a word is allowed only when that word is wider than the line. */
static int breaks_only_wide_words(const char *shown, const char *full, const lv_font_t *font, int line_w)
{
    const char *s = shown ? shown : "";
    const char *f = full ? full : "";
    while (*s || *f) {
        if (*s == '\n') {
            if (*f == ' ') {
                s++;
                f++;
                continue;
            }
            {
                const char *ws = f;
                const char *we;
                int w;
                while (ws > full && ws[-1] != ' ') {
                    ws--;
                }
                we = f;
                while (*we && *we != ' ') {
                    we++;
                }
                w = (int)lv_text_get_width(ws, (uint32_t)(we - ws), font, 0);
                if (w <= line_w) {
                    return 0;
                }
            }
            s++;
            continue;
        }
        if (!*s || !*f || *s != *f) {
            return 0;
        }
        s++;
        f++;
    }
    return 1;
}

/* A character-broken line must stay full, including lines after the first break
 * whose next source character is a space. A short "MM" before that space fails. */
static int glyph_fits_after(const char *line, uint32_t len, const char *next, const lv_font_t *font, int width)
{
    char buf[384];
    uint32_t n;
    int w;
    if (!next || !next[0] || next[0] == '\n' || !font || width < 1) {
        return 0;
    }
    n = t_utf8_next(next, 0);
    if (n == 0 || len + n >= sizeof buf) {
        return 0;
    }
    memcpy(buf, line, len);
    memcpy(buf + len, next, n);
    w = (int)lv_text_get_width(buf, len + n, font, 0);
    return w <= width;
}

static int filled_when_char_broken(const char *shown, const char *full, const lv_font_t *font, int width)
{
    const char *s = shown ? shown : "";
    const char *f = full ? full : "";
    const char *line = s;
    int char_mode = 0;
    while (*s || *f) {
        if (*s == '\n') {
            uint32_t len = (uint32_t)(s - line);
            if (*f != ' ') {
                char_mode = 1;
            }
            if (char_mode) {
                const char *next = f;
                while (*next == ' ') {
                    next++;
                }
                if (*next && *next != '\n' && glyph_fits_after(line, len, next, font, width)) {
                    return 0;
                }
            }
            if (*f == ' ') {
                f++;
            }
            s++;
            line = s;
            continue;
        }
        if (!*s || !*f || *s != *f) {
            return 0;
        }
        s++;
        f++;
    }
    return 1;
}

/* Sheet titles are "NAME is watering". The promise applies to NAME. */
static int promise_len(const char *full)
{
    const char *suf = " is watering";
    size_t n;
    size_t s;
    if (!full) {
        return 0;
    }
    n = strlen(full);
    s = strlen(suf);
    if (n >= s && memcmp(full + (n - s), suf, s) == 0) {
        return (int)(n - s);
    }
    return (int)n;
}

static void tile_policy(const char *full, int six, int title_w, int *allow_char, int *want_ellipsis,
                        const lv_font_t **font_out, int *first_w, int *rest_w, int *max_lines)
{
    const lv_font_t *big = six ? &zk_font_lit_20 : &zk_font_lit_26;
    const lv_font_t *floor = six ? &zk_font_lit_18 : &zk_font_lit_20;
    int big_lines = six ? 2 : 3;
    int floor_lines = six ? 3 : 4;
    int w = title_w > 0 ? title_w : 164;
    const lv_font_t *font;
    int lines;
    /* Word-wrap only. A character break is not a fit at the larger size, and
     * a running tile does not give up width for the drop. */
    if (oracle_words(full, big, w, w, big_lines)) {
        font = big;
        lines = big_lines;
    } else {
        font = floor;
        lines = floor_lines;
    }
    *font_out = font;
    *first_w = w;
    *rest_w = w;
    *max_lines = lines;
    if (oracle_words(full, font, w, w, lines)) {
        *allow_char = 0;
        *want_ellipsis = 0;
    } else if (oracle_chars(full, font, w, w, lines)) {
        *allow_char = 1;
        *want_ellipsis = 0;
    } else {
        *allow_char = 1;
        *want_ellipsis = 1;
    }
    if (promise_len(full) <= 24) {
        *want_ellipsis = 0;
    }
}

static const zk_ui_label_box_t *find_fmt(const zk_ui_label_box_t *b, int n, const char *fmt, int index)
{
    char id[32];
    snprintf(id, sizeof id, fmt, index);
    return find_box(b, n, id);
}

static void assert_icon_clear(const zk_ui_label_box_t *b, int n, int index, const char *what)
{
    zk_ui_tile_dbg_t tile;
    const zk_ui_label_box_t *title;
    const zk_ui_label_box_t *slot;
    int tw;
    TCHECK(zk_ui_debug_tile(index, &tile) == 1, "%s tile", what);
    if (!tile.on) {
        return;
    }
    title = find_fmt(b, n, "tile_title%d", index);
    slot = find_fmt(b, n, "tile_slot%d", index);
    TCHECK(tile.drop_visible, "%s drop hidden", what);
    TEQ_I(tile.drop_w, 20);
    TEQ_I(tile.drop_h, 20);
    TEQ_I(tile.line1_pad, 0);
    if (!title || !slot || !slot->visible) {
        TCHECK(slot && slot->visible, "%s status line", what);
        return;
    }
    /* The name keeps the full box. The icon sits in the status row, clear of it. */
    TCHECK(title->y + title->h <= tile.drop_y, "%s drop overlaps the name", what);
    TCHECK(tile.drop_y >= slot->parent_y && tile.drop_y + tile.drop_h <= slot->parent_y + slot->parent_h,
           "%s drop outside the status row", what);
    TCHECK(slot->x >= tile.drop_x + tile.drop_w + 2, "%s status ink %d meets drop %d+%d", what, slot->x, tile.drop_x,
           tile.drop_w);
    tw = (int)lv_text_get_width(slot->text, (uint32_t)strlen(slot->text), slot->font, 0);
    TCHECK(slot->x + tw <= slot->parent_x + slot->parent_w + 1, "%s status %d px past the slot [%s]", what, tw,
           slot->text);
}

static void assert_tile(const zk_ui_label_box_t *b, int n, int index, const char *full, const char *what)
{
    const zk_ui_label_box_t *title = find_fmt(b, n, "tile_title%d", index);
    const zk_ui_label_box_t *slot = find_fmt(b, n, "tile_slot%d", index);
    const zk_ui_label_box_t *ln = find_fmt(b, n, "tile_ln%d", index);
    const zk_ui_label_box_t *rest = find_fmt(b, n, "tile_rest%d", index);
    zk_ui_tile_dbg_t tile;
    int allow_char = 0;
    int want_ellipsis = 0;
    int first_w = 0;
    int rest_w = 0;
    int max_lines = 0;
    int six;
    const lv_font_t *font = NULL;
    int ell_w;
    TCHECK(title != NULL, "%s title", what);
    TCHECK(zk_ui_debug_tile(index, &tile) == 1, "%s dbg", what);
    if (!title || !tile.present) {
        return;
    }
    six = title->parent_h > 0 && title->parent_h < 140;
    tile_policy(full, six, tile.title_w, &allow_char, &want_ellipsis, &font, &first_w, &rest_w, &max_lines);
    TEQ_I(tile.line1_pad, 0);
    TEQ_I(title->x - title->parent_x, 10);
    TCHECK(title->font == font, "%s font", what);
    TCHECK(count_lines(title->text) <= max_lines, "%s lines %d [%s]", what, count_lines(title->text), title->text);
    TCHECK(lines_fit_widths(title->text, title->font, first_w, rest_w, max_lines), "%s ink [%s] %d/%d", what,
           title->text, first_w, rest_w);
    ell_w = strchr(title->text, '\n') ? rest_w : first_w;
    if (promise_len(full) <= 24) {
        TCHECK(strstr(title->text, "\xE2\x80\xA6") == NULL, "%s 24-char ellipsis [%s]", what, title->text);
        TCHECK(!title->clamped, "%s 24-char clamped [%s]", what, title->text);
    }
    if (want_ellipsis) {
        TCHECK(title->clamped, "%s not clamped [%s]", what, title->text);
        TCHECK(ellipsis_rule(title->text, full, title->font, ell_w), "%s ellipsis [%s]", what, title->text);
    } else {
        TCHECK(text_covers(title->text, full, allow_char), "%s text [%s] want [%s]", what, title->text, full);
        TCHECK(!title->clamped, "%s clamped [%s]", what, title->text);
        TCHECK(strstr(title->text, "\xE2\x80\xA6") == NULL, "%s ellipsis [%s]", what, title->text);
        TCHECK(breaks_only_wide_words(title->text, full, title->font, first_w > rest_w ? rest_w : first_w),
               "%s mid-word [%s]", what, title->text);
        if (allow_char) {
            TCHECK(filled_when_char_broken(title->text, full, title->font, first_w), "%s short char break [%s]", what,
                   title->text);
        }
    }
    if (slot && title->font) {
        int lh = (int)lv_font_get_line_height(title->font);
        int drawn = lh > 0 ? title->h / lh : 0;
        if (six && title->font == &zk_font_lit_18 && drawn >= 3) {
            TEQ_I(title->y - title->parent_y, 4);
            TEQ_I(slot->parent_y - title->parent_y, 74);
        }
        if (!six && title->font == &zk_font_lit_20 && drawn >= 4) {
            TEQ_I(title->y - title->parent_y, 4);
            TEQ_I(slot->parent_y - title->parent_y, 100);
        }
    }
    if (tile.on) {
        assert_icon_clear(b, n, index, what);
    }
    if (ln && ln->visible) {
        TCHECK(rest && rest->visible, "%s rest hidden", what);
        TCHECK(hang_lines_fit(title->text, ln, rest), "%s hang [%s] ln %d rest %d", what, title->text, ln->w, rest->w);
        assert_box_clear(ln, slot, NULL, what);
        if (rest && rest->visible) {
            assert_box_clear(rest, slot, NULL, what);
            if (!want_ellipsis) {
                TCHECK(strstr(rest->text, "\xE2\x80\xA6") == NULL, "%s rest ellipsis [%s]", what, rest->text);
            }
        }
        if (!want_ellipsis && ln->text[0]) {
            TCHECK(strstr(ln->text, "\xE2\x80\xA6") == NULL, "%s line ellipsis", what);
        }
        return;
    }
    if (!want_ellipsis) {
        assert_wrap(title, what);
    }
    assert_box_clear(title, slot, NULL, what);
}

static void assert_hero_rule(const zk_ui_label_box_t *b, int n, const char *full, const char *what);
static void assert_sheet_rule(const zk_ui_label_box_t *b, int n, const char *full, const char *what);

/* Idle Literata headline is the next-run time (spec). The station name is on the tile. */
static void assert_idle_hero_is_time(const zk_ui_label_box_t *b, int n, const char *name, const char *what)
{
    const zk_ui_label_box_t *head = find_box(b, n, "hero_headline");
    const zk_ui_label_box_t *sub = find_box(b, n, "hero_secondary");
    TCHECK(head != NULL, "%s idle headline", what);
    if (!head) {
        return;
    }
    TCHECK(strstr(head->text, "\xE2\x80\xA6") == NULL, "%s idle headline ellipsis [%s]", what, head->text);
    TCHECK(!head->clamped, "%s idle headline clamped", what);
    TCHECK(strstr(head->text, "AM") != NULL || strstr(head->text, "PM") != NULL, "%s idle headline [%s]", what,
           head->text);
    TCHECK(strcmp(head->text, name) != 0, "%s idle headline is the station name", what);
    assert_box_clear(head, sub, NULL, what);
}

static void check_name_slots(const char *name)
{
    zk_ui_label_box_t b[96];
    char dir[64];
    char sheet[160];
    int n;
    snprintf(dir, sizeof dir, "/tmp/zk-names-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, name, 4, 1) == 0, "scene water4 %s", name);
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load water4 %s", name);
    n = grab(b, 96);
    if (n > 0) {
        assert_hero_rule(b, n, name, "hero watering");
        assert_tile(b, n, 0, name, "4-up running");
        zk_ui_debug_set_view(ZK_SCREEN_SHEET_STOP_STATION, 0, 0);
        zk_ui_refresh();
        n = grab(b, 96);
        snprintf(sheet, sizeof sheet, "%s is watering", name);
        if (n > 0) {
            assert_sheet_rule(b, n, sheet, "sheet title");
        }
    }
    drop_scene(dir);

    snprintf(dir, sizeof dir, "/tmp/zk-names-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, name, 4, 2) == 0, "scene manual %s", name);
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load manual %s", name);
    n = grab(b, 96);
    if (n > 0) {
        assert_hero_rule(b, n, name, "hero manual");
    }
    drop_scene(dir);

    snprintf(dir, sizeof dir, "/tmp/zk-names-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, name, 4, 0) == 0, "scene idle4 %s", name);
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load idle4 %s", name);
    n = grab(b, 96);
    if (n > 0) {
        assert_idle_hero_is_time(b, n, name, "hero idle");
        assert_tile(b, n, 0, name, "4-up idle");
    }
    drop_scene(dir);

    snprintf(dir, sizeof dir, "/tmp/zk-names-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, name, 6, 1) == 0, "scene water6 %s", name);
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load water6 %s", name);
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, name, "6-up running");
    }
    drop_scene(dir);

    snprintf(dir, sizeof dir, "/tmp/zk-names-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, name, 6, 0) == 0, "scene idle6 %s", name);
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load idle6 %s", name);
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, name, "6-up idle");
    }
    drop_scene(dir);
}

static void test_names_24(void)
{
    static const char *names[] = {
        "NORTH MEADOW WILDFLOWERS",
        "Hummingbird Mowed Meadow",
        "Backyard Vegetable Patch",
        "Wildflower Meadow Waters",
        "Front Yard Drip Emitters",
    };
    char wide[25];
    size_t i;
    memset(wide, 'W', 24);
    wide[24] = 0;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        TEQ_I((int)strlen(names[i]), 24);
        check_name_slots(names[i]);
    }
    TEQ_I((int)strlen(wide), 24);
    check_name_slots(wide);
}

static int write_schedules(const char *dir, const char *const *names, int count)
{
    char path[256];
    FILE *f;
    int i;
    if (!dir || !names || count < 1) {
        return -1;
    }
    snprintf(path, sizeof path, "%s/schedules.json", dir);
    f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fputs("{\"schedules\":[\n", f);
    for (i = 0; i < count; i++) {
        fprintf(f,
                "{\"id\":\"p%d\",\"name\":\"%s\",\"enabled\":%s,\"start\":\"0%d:10\","
                "\"weekdays\":[\"mon\",\"tue\",\"wed\",\"thu\",\"fri\",\"sat\",\"sun\"],"
                "\"steps\":[{\"station_id\":\"s0\",\"minutes\":5}]}%s\n",
                i, names[i], i == count - 1 ? "false" : "true", 5 + i, i + 1 < count ? "," : "");
    }
    fputs("]}\n", f);
    if (fclose(f) != 0) {
        return -1;
    }
    return 0;
}

static void fill_w(char *dst, size_t cap)
{
    size_t o = 0;
    int w;
    int i;
    if (!dst || cap == 0) {
        return;
    }
    for (w = 0; w < 5; w++) {
        if (w && o + 1 < cap) {
            dst[o++] = ' ';
        }
        for (i = 0; i < 10 && o + 1 < cap; i++) {
            dst[o++] = 'W';
        }
    }
    dst[o] = 0;
}

static void assert_sched_ink(const zk_ui_label_box_t *b, int n, const char *what)
{
    static const char *ids[] = {"sched_name0", "sched_name0b", "sched_name1", "sched_name1b", "sched_meta0",
                                "sched_meta1", "sched_note",    "sched_label", NULL};
    const zk_ui_label_box_t *next = find_box(b, n, "next_word");
    int i;
    for (i = 0; ids[i]; i++) {
        const zk_ui_label_box_t *t = find_box(b, n, ids[i]);
        int maxl = strncmp(ids[i], "sched_name", 10) == 0 ? 2 : 1;
        if (!t || !t->visible) {
            continue;
        }
        TCHECK(count_lines(t->text) <= maxl, "%s %s lines %d [%s]", what, ids[i], count_lines(t->text), t->text);
        TCHECK(lines_fit_widths(t->text, t->font, t->w, t->w, maxl), "%s %s wider than %d [%s]", what, ids[i], t->w,
               t->text);
        TCHECK(inside_parent(t), "%s %s outside parent", what, ids[i]);
        if (next && next->visible) {
            TCHECK(!box_hits(t, next->parent_x, next->parent_y, next->parent_w, next->parent_h),
                   "%s %s hits Next (%d,%d %dx%d) vs parent (%d,%d %dx%d)", what, ids[i], t->x, t->y, t->w, t->h,
                   next->parent_x, next->parent_y, next->parent_w, next->parent_h);
        }
    }
}

static void assert_hero_rule(const zk_ui_label_box_t *b, int n, const char *full, const char *what)
{
    const zk_ui_label_box_t *head = find_box(b, n, "hero_headline");
    static const lv_font_t *faces[] = {&zk_font_lit_48, &zk_font_lit_40, &zk_font_lit_32};
    const lv_font_t *expect = NULL;
    int allow = 0;
    int fits = 0;
    int i;
    TCHECK(head != NULL, "%s headline", what);
    if (!head) {
        return;
    }
    TEQ_I(head->w, 332);
    TCHECK(count_lines(head->text) <= 3, "%s lines [%s]", what, head->text);
    TCHECK(lines_fit_widths(head->text, head->font, head->w, head->w, 3), "%s ink [%s]", what, head->text);
    expect = &zk_font_lit_32;
    for (i = 0; i < 3; i++) {
        if (oracle_words(full, faces[i], 332, 332, 3)) {
            fits = 1;
            allow = 0;
            expect = faces[i];
            break;
        }
    }
    if (!fits && oracle_chars(full, &zk_font_lit_32, 332, 332, 3)) {
        fits = 1;
        allow = 1;
        expect = &zk_font_lit_32;
    }
    if (full && (int)strlen(full) <= 24) {
        TCHECK(fits, "%s 24-char name does not fit [%s]", what, head->text);
        TCHECK(strstr(head->text, "\xE2\x80\xA6") == NULL, "%s 24-char ellipsis [%s]", what, head->text);
    }
    if (fits) {
        TCHECK(head->font == expect, "%s stepped past a size that fits", what);
        TCHECK(text_covers(head->text, full, allow), "%s text [%s]", what, head->text);
        TCHECK(strstr(head->text, "\xE2\x80\xA6") == NULL, "%s ellipsis [%s]", what, head->text);
        TCHECK(breaks_only_wide_words(head->text, full, head->font, 332), "%s mid-word [%s]", what, head->text);
        if (allow) {
            TCHECK(filled_when_char_broken(head->text, full, head->font, 332), "%s short char break [%s]", what,
                   head->text);
        }
    } else {
        TCHECK(ellipsis_rule(head->text, full, head->font, 332), "%s ellipsis [%s]", what, head->text);
    }
    assert_box_clear(head, find_box(b, n, "hero_time"), find_box(b, n, "hero_secondary"), what);
}

static void assert_sheet_rule(const zk_ui_label_box_t *b, int n, const char *full, const char *what)
{
    const zk_ui_label_box_t *title = find_box(b, n, "sheet_title");
    const lv_font_t *expect = &zk_font_lit_26;
    int allow = 0;
    int ell = 0;
    TCHECK(title != NULL, "%s sheet", what);
    if (!title) {
        return;
    }
    TCHECK(count_lines(title->text) <= 2, "%s lines [%s]", what, title->text);
    TCHECK(lines_fit_widths(title->text, title->font, title->w, title->w, 2), "%s ink [%s] w=%d", what, title->text,
           title->w);
    if (oracle_words(full, &zk_font_lit_32, 728, 728, 2)) {
        allow = 0;
        expect = &zk_font_lit_32;
    } else if (oracle_words(full, &zk_font_lit_26, 728, 728, 2)) {
        allow = 0;
        expect = &zk_font_lit_26;
    } else if (oracle_chars(full, &zk_font_lit_26, 728, 728, 2)) {
        allow = 1;
        expect = &zk_font_lit_26;
    } else {
        ell = 1;
        expect = &zk_font_lit_26;
    }
    if (promise_len(full) <= 24) {
        ell = 0;
        TCHECK(strstr(title->text, "\xE2\x80\xA6") == NULL, "%s 24-char ellipsis [%s]", what, title->text);
    }
    if (!ell) {
        TCHECK(title->font == expect, "%s stepped past a size that fits", what);
        TCHECK(text_covers(title->text, full, allow), "%s text [%s] want [%s]", what, title->text, full);
        TCHECK(strstr(title->text, "\xE2\x80\xA6") == NULL, "%s ellipsis [%s]", what, title->text);
        TCHECK(breaks_only_wide_words(title->text, full, title->font, 728), "%s mid-word [%s]", what, title->text);
        if (allow) {
            TCHECK(filled_when_char_broken(title->text, full, title->font, 728), "%s short char break [%s]", what,
                   title->text);
        }
    } else {
        TCHECK(ellipsis_rule(title->text, full, title->font, title->w), "%s ellipsis [%s]", what, title->text);
    }
    assert_box_clear(title, find_box(b, n, "sheet_time"), NULL, what);
}

static void test_sched_longname(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *name;
    const zk_ui_label_box_t *nameb;
    const zk_ui_label_box_t *fold;
    const zk_ui_label_box_t *next;
    const char *full = "test patio.sh parity: east 4, upper 2, lower 5 (bash cron 05:47) \xC2\xB7 Daily 5:47 AM";
    const char *l1 = "test patio.sh parity: east 4, upper 2, lower 5";
    const char *l2 = "(bash cron 05:47) \xC2\xB7 Daily 5:47 AM";
    const char *folded = "Drip \xE2\x80\x93 \"Thu\" \xC2\xB7 Jard\xC3\xADn";
    char expect[160];
    char more[80];
    int w1;
    int wmore;
    int w2;
    TCHECK((int)strlen(full) == 81, "synthetic name bytes %d", (int)strlen(full));
    TCHECK(load_fix("dc-schedules-longname", ZK_SCREEN_SCHEDULES) == 0, "load longname schedules");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    expect_parent(b, n, "next_word", 400, 232, 364, 96);
    name = find_box(b, n, "sched_name0");
    nameb = find_box(b, n, "sched_name0b");
    fold = find_box(b, n, "sched_name1");
    next = find_box(b, n, "next_word");
    TCHECK(name && fold && next, "longname rows");
    snprintf(more, sizeof more, "%s (bash", l1);
    w1 = (int)lv_text_get_width(l1, (uint32_t)strlen(l1), &zk_font_lit_32, 0);
    wmore = (int)lv_text_get_width(more, (uint32_t)strlen(more), &zk_font_lit_32, 0);
    w2 = (int)lv_text_get_width(l2, (uint32_t)strlen(l2), &zk_font_lit_32, 0);
    TCHECK(w1 <= 728 && wmore > 728 && w2 <= 728, "measured wrap %d / %d / %d", w1, wmore, w2);
    snprintf(expect, sizeof expect, "%s\n%s", l1, l2);
    if (name) {
        TEQ_S(name->text, expect);
        TCHECK(strstr(name->text, "\xE2\x80\xA6") == NULL, "long name ellipsized");
        TEQ_I(name->x, 38);
        TEQ_I(name->y, 66);
        TEQ_I(name->w, 728);
        TEQ_I(name->h, 70);
        TCHECK(name->visible, "long name hidden");
    }
    if (nameb) {
        TCHECK(!nameb->visible, "equal-width name split");
    }
    if (fold) {
        TEQ_S(fold->text, folded);
        TCHECK(strstr(fold->text, "\xF0\x9F\x92\xA7") == NULL, "emoji survived");
        TCHECK(strstr(fold->text, "\xE2\x80\x94") == NULL, "em dash survived");
    }
    if (next) {
        TCHECK(next->visible, "Next hidden");
    }
    assert_sched_ink(b, n, "longname");
    {
        const zk_ui_label_box_t *meta1 = find_box(b, n, "sched_meta1");
        const zk_ui_label_box_t *note = find_box(b, n, "sched_note");
        if (meta1) {
            TEQ_I(meta1->w, 350);
        }
        if (note) {
            TEQ_I(note->w, 350);
        }
    }
    zk_ui_debug_set_sched(2);
    zk_ui_refresh();
    n = grab(b, 96);
    name = find_box(b, n, "sched_name0");
    TCHECK(name != NULL, "page 2 name");
    if (name) {
        TEQ_S(name->text, "Drip");
        TCHECK(strstr(name->text, "\xF0\x9F\x92\xA7") == NULL, "page 2 emoji");
    }
    assert_sched_ink(b, n, "longname-p2");
}

static void test_sched_paused_long(void)
{
    zk_ui_label_box_t b[96];
    int n;
    const zk_ui_label_box_t *name;
    const zk_ui_label_box_t *next;
    const char *full = "live parity: alpha 2, beta 3, gamma 3 (bash cron 08:23) \xC2\xB7 Daily 8:23 AM";
    TCHECK(load_fix("paused-long", ZK_SCREEN_SCHEDULES) == 0, "load paused-long schedules");
    n = grab(b, 96);
    if (n < 0) {
        return;
    }
    name = find_box(b, n, "sched_name0");
    next = find_box(b, n, "next_word");
    TCHECK(name != NULL, "paused-long name");
    if (next) {
        TCHECK(!next->visible, "Next shown for two programs");
    }
    if (name) {
        TCHECK(count_lines(name->text) <= 2, "paused-long lines [%s]", name->text);
        TCHECK(lines_fit_widths(name->text, name->font, name->w, name->w, 2), "paused-long ink [%s]", name->text);
        if (oracle_words(full, &zk_font_lit_32, 728, 728, 2)) {
            TCHECK(text_covers(name->text, full, 0), "paused-long text [%s]", name->text);
            TCHECK(strstr(name->text, "\xE2\x80\xA6") == NULL, "paused-long ellipsis [%s]", name->text);
            TCHECK(breaks_only_wide_words(name->text, full, name->font, 728), "paused-long break [%s]", name->text);
        } else {
            TCHECK(ellipsis_rule(name->text, full, name->font, name->w), "paused-long ellipsis [%s]", name->text);
        }
    }
    assert_sched_ink(b, n, "paused-long");
}

static void test_sched_w_stress(void)
{
    zk_ui_label_box_t b[96];
    char dir[64];
    char wname[80];
    const char *names[3];
    int n;
    const zk_ui_label_box_t *name;
    const zk_ui_label_box_t *nameb;
    const zk_ui_label_box_t *row1;
    const zk_ui_label_box_t *row1b;
    const zk_ui_label_box_t *next;
    fill_w(wname, sizeof wname);
    names[0] = wname;
    names[1] = wname;
    names[2] = "Short";
    snprintf(dir, sizeof dir, "/tmp/zk-wsched-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, "Beds", 4, 0) == 0, "w scene");
    TCHECK(write_schedules(dir, names, 3) == 0, "w schedules");
    TCHECK(load_abs(dir, ZK_SCREEN_SCHEDULES) == 0, "load w schedules");
    n = grab(b, 96);
    if (n > 0) {
        name = find_box(b, n, "sched_name0");
        nameb = find_box(b, n, "sched_name0b");
        row1 = find_box(b, n, "sched_name1");
        row1b = find_box(b, n, "sched_name1b");
        next = find_box(b, n, "next_word");
        TCHECK(next && next->visible, "w Next");
        TCHECK(name != NULL, "w name0");
        if (name) {
            TCHECK(count_lines(name->text) <= 2, "w lines [%s]", name->text);
            TCHECK(ellipsis_rule(name->text, wname, name->font, name->w), "w ellipsis [%s]", name->text);
            TEQ_I(name->w, 728);
        }
        if (nameb) {
            TCHECK(!nameb->visible, "row 0 split at equal width");
        }
        TCHECK(row1 && row1b, "w row 1");
        if (row1) {
            TEQ_I(row1->w, 728);
            TCHECK(strchr(row1->text, '\n') == NULL, "row 1 line 1 holds a break");
        }
        if (row1 && row1b && row1b->visible) {
            char joined[ZK_UI_LABEL_TEXT_MAX * 2];
            size_t n1 = strlen(row1->text);
            size_t n2 = strlen(row1b->text);
            TEQ_I(row1b->w, 350);
            joined[0] = 0;
            if (n1 + n2 + 2 < sizeof joined) {
                memcpy(joined, row1->text, n1);
                joined[n1] = '\n';
                memcpy(joined + n1 + 1, row1b->text, n2);
                joined[n1 + n2 + 1] = 0;
            }
            TCHECK(ellipsis_rule(joined, wname, row1b->font, 350), "row1 joined [%s]", joined);
            TCHECK(strstr(row1b->text, "\xE2\x80\xA6") != NULL, "row1b no ellipsis [%s]", row1b->text);
            TCHECK(lines_fit_widths(row1b->text, row1b->font, 350, 350, 1), "row1b ink [%s]", row1b->text);
        } else {
            TCHECK(0, "row 1 line 2 was not split to 350");
        }
        assert_sched_ink(b, n, "w-stress");
    }
    drop_scene(dir);
}

static void test_wide_words(void)
{
    zk_ui_label_box_t b[96];
    char dir[64];
    char sheet[80];
    const char *full = "MMMMMMMMMMMM MMMMMMMMMMM";
    const char *one[] = {full};
    int n;
    TEQ_I((int)strlen(full), 24);
    {
        int w10 = (int)lv_text_get_width("MMMMMMMMMM", 10, &zk_font_lit_32, 0);
        int w11 = (int)lv_text_get_width("MMMMMMMMMMM", 11, &zk_font_lit_32, 0);
        /* Measured advance, not a hand count. Ten M fit the 332 px hero; eleven do not. */
        TCHECK(w10 <= 332 && w11 > 332, "lit_32 ten M is %d, eleven is %d", w10, w11);
    }
    snprintf(dir, sizeof dir, "/tmp/zk-wide-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, full, 4, 1) == 0, "wide water4");
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load wide water4");
    n = grab(b, 96);
    if (n > 0) {
        assert_hero_rule(b, n, full, "wide hero");
        assert_tile(b, n, 0, full, "wide 4 running");
        zk_ui_debug_set_view(ZK_SCREEN_SHEET_STOP_STATION, 0, 0);
        zk_ui_refresh();
        n = grab(b, 96);
        snprintf(sheet, sizeof sheet, "%s is watering", full);
        if (n > 0) {
            assert_sheet_rule(b, n, sheet, "wide sheet");
        }
    }
    drop_scene(dir);

    snprintf(dir, sizeof dir, "/tmp/zk-wide-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, full, 4, 0) == 0, "wide idle4");
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load wide idle4");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, full, "wide 4 idle");
    }
    drop_scene(dir);

    snprintf(dir, sizeof dir, "/tmp/zk-wide-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, full, 6, 1) == 0, "wide water6");
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load wide water6");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, full, "wide 6 running");
    }
    drop_scene(dir);

    snprintf(dir, sizeof dir, "/tmp/zk-wide-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, full, 6, 0) == 0, "wide idle6");
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load wide idle6");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, full, "wide 6 idle");
    }
    drop_scene(dir);

    TCHECK(load_fix("dc-wide-4", ZK_SCREEN_HOME) == 0, "shot wide4");
    n = grab(b, 96);
    if (n > 0) {
        assert_hero_rule(b, n, full, "shot wide hero");
        assert_tile(b, n, 2, full, "shot wide 4 running");
        zk_ui_debug_set_view(ZK_SCREEN_SHEET_STOP_STATION, 0, 2);
        zk_ui_refresh();
        n = grab(b, 96);
        snprintf(sheet, sizeof sheet, "%s is watering", full);
        if (n > 0) {
            assert_sheet_rule(b, n, sheet, "shot wide sheet");
        }
    }
    TCHECK(load_fix("dc-wide-4-idle", ZK_SCREEN_HOME) == 0, "shot wide4 idle");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, full, "shot wide 4 idle");
    }
    TCHECK(load_fix("dc-wide-6", ZK_SCREEN_HOME) == 0, "shot wide6");
    n = grab(b, 96);
    if (n > 0) {
        assert_hero_rule(b, n, full, "shot wide6 hero");
        assert_tile(b, n, 2, full, "shot wide 6 running");
    }
    TCHECK(load_fix("dc-wide-6-idle", ZK_SCREEN_HOME) == 0, "shot wide6 idle");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, full, "shot wide 6 idle");
    }

    snprintf(dir, sizeof dir, "/tmp/zk-wide-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, "Beds", 4, 0) == 0, "wide sched scene");
    TCHECK(write_schedules(dir, one, 1) == 0, "wide sched json");
    TCHECK(load_abs(dir, ZK_SCREEN_SCHEDULES) == 0, "load wide sched");
    n = grab(b, 96);
    if (n > 0) {
        const zk_ui_label_box_t *name = find_box(b, n, "sched_name0");
        TCHECK(name != NULL, "wide sched name");
        if (name) {
            TCHECK(count_lines(name->text) <= 2, "wide sched lines [%s]", name->text);
            TCHECK(oracle_words(full, &zk_font_lit_32, 728, 728, 2), "wide words do not fit two schedule lines");
            TCHECK(text_covers(name->text, full, 0), "wide sched text [%s]", name->text);
            TCHECK(strstr(name->text, "\xE2\x80\xA6") == NULL, "wide sched ellipsis");
            TCHECK(breaks_only_wide_words(name->text, full, name->font, 728), "wide sched break [%s]", name->text);
            TCHECK(lines_fit_widths(name->text, name->font, name->w, name->w, 2), "wide sched ink");
        }
        assert_sched_ink(b, n, "wide-sched");
    }
    drop_scene(dir);
}

static void test_running_icon(void)
{
    zk_ui_label_box_t b[96];
    char dir[64];
    char ws[25];
    int n;
    const char *meadow = "NORTH MEADOW WILDFLOWERS";
    memset(ws, 'W', 24);
    ws[24] = 0;
    TCHECK(load_fix("dc-icon-meadow-6", ZK_SCREEN_HOME) == 0, "meadow shot");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 2, meadow, "meadow 6 running");
    }
    TCHECK(load_fix("dc-icon-w-4", ZK_SCREEN_HOME) == 0, "w4 shot");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 2, ws, "24W 4 running");
    }
    TCHECK(load_fix("dc-icon-w-6", ZK_SCREEN_HOME) == 0, "w6 shot");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 2, ws, "24W 6 running");
    }
    snprintf(dir, sizeof dir, "/tmp/zk-icon-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, meadow, 6, 1) == 0, "meadow scene");
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load meadow");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, meadow, "meadow scene running");
    }
    drop_scene(dir);
    snprintf(dir, sizeof dir, "/tmp/zk-icon-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, ws, 4, 1) == 0, "w4 scene");
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load w4");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, ws, "24W scene 4");
    }
    drop_scene(dir);
    snprintf(dir, sizeof dir, "/tmp/zk-icon-XXXXXX");
    TCHECK(write_scene(dir, sizeof dir, ws, 6, 1) == 0, "w6 scene");
    TCHECK(load_abs(dir, ZK_SCREEN_HOME) == 0, "load w6");
    n = grab(b, 96);
    if (n > 0) {
        assert_tile(b, n, 0, ws, "24W scene 6");
    }
    drop_scene(dir);
}

static char g_uniq[800][496];
static int g_nuniq;

static void add_str(const char *s)
{
    int i;
    if (!s || !s[0]) {
        return;
    }
    for (i = 0; i < g_nuniq; i++) {
        if (strcmp(g_uniq[i], s) == 0) {
            return;
        }
    }
    if (g_nuniq >= 800) {
        TCHECK(0, "glyph string table full");
        return;
    }
    snprintf(g_uniq[g_nuniq], sizeof g_uniq[0], "%s", s);
    g_nuniq++;
}

static void add_view(const zk_view_t *v)
{
    int i;
    if (!v) {
        return;
    }
    add_str(v->hero.label);
    add_str(v->hero.headline);
    add_str(v->hero.time_line);
    add_str(v->hero.secondary);
    add_str(v->hero.rain_text);
    for (i = 0; i < v->n_tiles && i < ZK_MAX_SHOWN; i++) {
        add_str(v->tiles[i].name);
        add_str(v->tiles[i].slot);
    }
    add_str(v->sheet.label);
    add_str(v->sheet.title);
    add_str(v->sheet.body);
    add_str(v->sheet.time_line);
    add_str(v->bar_word);
    add_str(v->overlap_title);
    add_str(v->overlap_body);
    add_str(v->sched.label);
    add_str(v->sched.note);
    add_str(v->sched.empty_title);
    add_str(v->sched.empty_body);
    for (i = 0; i < v->sched.n_rows && i < 2; i++) {
        add_str(v->sched.rows[i].name);
        add_str(v->sched.rows[i].meta);
    }
}

static int take_cp(const char *s, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    if (!u || !u[0]) {
        return 0;
    }
    if (u[0] < 0x80) {
        *cp = u[0];
        return 1;
    }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        return *cp >= 0x80 ? 2 : -1;
    }
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        return *cp >= 0x800 ? 3 : -1;
    }
    if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) | ((uint32_t)(u[2] & 0x3F) << 6) |
              (u[3] & 0x3F);
        return (*cp >= 0x10000 && *cp <= 0x10FFFF) ? 4 : -1;
    }
    return -1;
}

static void test_glyphs(void)
{
    static const struct {
        const char *name;
        const lv_font_t *font;
    } fonts[] = {
        {"r_20", &zk_font_r_20},   {"r_22", &zk_font_r_22},   {"r_24", &zk_font_r_24},   {"r_26", &zk_font_r_26},
        {"m_18", &zk_font_m_18},   {"m_20", &zk_font_m_20},   {"m_26", &zk_font_m_26},   {"m_28", &zk_font_m_28},
        {"sb_18", &zk_font_sb_18}, {"sb_24", &zk_font_sb_24}, {"sb_26", &zk_font_sb_26}, {"sb_36", &zk_font_sb_36},
        {"b_30", &zk_font_b_30},   {"b_36", &zk_font_b_36},   {"lit_18", &zk_font_lit_18}, {"lit_20", &zk_font_lit_20},
        {"lit_26", &zk_font_lit_26}, {"lit_32", &zk_font_lit_32}, {"lit_40", &zk_font_lit_40}, {"lit_48", &zk_font_lit_48},
    };
    static const char *lits[] = {
        ZK_NBSP, ZK_MID, ZK_EN, ZK_RQ, ZK_WORD_PAUSE, ZK_WORD_RESUME, ZK_WORD_SCHEDULES, ZK_WORD_HOME, ZK_WORD_NEXT,
        ZK_SCHED_NOTE, ZK_SCHED_EMPTY, ZK_SCHED_EMPTY_BODY, ZK_SCHED_READ, ZK_STOP_PENDING_HEADLINE,
        ZK_STOP_PENDING_LABEL, ZK_STOP_PENDING_RETRY, ZK_STOP_PENDING_OFFLINE, ZK_WORD_STOP, ZK_WORD_CANCEL,
        ZK_WORD_RUN, ZK_WORD_STOP_ALL, ZK_WORD_OK, ZK_PAUSE_BODY,
    };
    const char *root = NULL;
    struct stat st;
    DIR *d;
    struct dirent *de;
    zk_kiosk_t *kiosk;
    zk_schedules_t *sched;
    zk_soil_doc_t *soil;
    zk_last_run_t *lasts;
    zk_view_t *view;
    zk_soil_cfg_t cfg;
    int bad = 0;
    int noted = 0;
    size_t fi;
    int si;
    lv_font_glyph_dsc_t space;
    if (stat("tests/fixtures", &st) == 0) {
        root = "tests/fixtures";
    } else if (stat("native-kiosk/tests/fixtures", &st) == 0) {
        root = "native-kiosk/tests/fixtures";
    }
    TCHECK(LV_USE_FONT_PLACEHOLDER == 1, "placeholder tripwire");
    memset(&space, 0, sizeof space);
    TCHECK(lv_font_get_glyph_dsc(&zk_font_lit_32, &space, (uint32_t)' ', 0) && !space.is_placeholder, "space glyph");
    g_nuniq = 0;
    for (si = 0; si < (int)(sizeof lits / sizeof lits[0]); si++) {
        add_str(lits[si]);
    }
    TCHECK(root != NULL, "fixtures root");
    if (!root) {
        return;
    }
    kiosk = calloc(1, sizeof *kiosk);
    sched = calloc(1, sizeof *sched);
    soil = calloc(1, sizeof *soil);
    lasts = calloc(ZK_MAX_STATIONS, sizeof *lasts);
    view = calloc(1, sizeof *view);
    TCHECK(kiosk && sched && soil && lasts && view, "glyph alloc");
    if (!kiosk || !sched || !soil || !lasts || !view) {
        free(kiosk);
        free(sched);
        free(soil);
        free(lasts);
        free(view);
        return;
    }
    zk_soil_cfg_defaults(&cfg);
    d = opendir(root);
    TCHECK(d != NULL, "opendir %s", root);
    if (d) {
        while ((de = readdir(d)) != NULL) {
            char path[512];
            char *kbuf;
            char *sbuf;
            char *soilbuf;
            char *hbuf;
            zk_copy_src_t src;
            int sheets[] = {ZK_SHEET_NONE, ZK_SHEET_RUN, ZK_SHEET_ANYWAY, ZK_SHEET_STOP_STATION, ZK_SHEET_STOP_ALL,
                            ZK_SHEET_OVERLAP};
            int phases[] = {ZK_STOP_IDLE, ZK_STOP_QUEUED, ZK_STOP_WAIT, ZK_STOP_CONFIRMED};
            int vi;
            if (de->d_name[0] == '.') {
                continue;
            }
            snprintf(path, sizeof path, "%s/%s", root, de->d_name);
            if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                continue;
            }
            snprintf(path, sizeof path, "%s/%s/kiosk.json", root, de->d_name);
            kbuf = zk_read_file(path);
            if (!kbuf) {
                continue;
            }
            if (zk_parse_kiosk(kbuf, kiosk) != ZK_OK) {
                TCHECK(0, "parse %s", de->d_name);
                free(kbuf);
                continue;
            }
            snprintf(path, sizeof path, "%s/%s/schedules.json", root, de->d_name);
            sbuf = zk_read_file(path);
            memset(sched, 0, sizeof *sched);
            if (sbuf && zk_parse_schedules(sbuf, sched) != ZK_OK) {
                TCHECK(0, "schedules %s", de->d_name);
            }
            snprintf(path, sizeof path, "%s/%s/soil.json", root, de->d_name);
            soilbuf = zk_read_file(path);
            memset(soil, 0, sizeof *soil);
            snprintf(path, sizeof path, "%s/%s/history.json", root, de->d_name);
            hbuf = zk_read_file(path);
            memset(&src, 0, sizeof src);
            src.kiosk = kiosk;
            src.have_kiosk = 1;
            src.schedules = sched;
            src.have_schedules = sbuf && sched->n >= 0;
            if (soilbuf && zk_parse_soil_doc(soilbuf, soil) == ZK_OK) {
                src.soil_doc = soil;
            }
            if (hbuf) {
                int nl = zk_parse_history_lasts(hbuf, lasts, ZK_MAX_STATIONS);
                if (nl > 0) {
                    src.lasts = lasts;
                    src.n_lasts = nl;
                }
            }
            if (kiosk->has_now) {
                src.wall = kiosk->now;
            }
            for (vi = 0; vi < (int)(sizeof sheets / sizeof sheets[0]); vi++) {
                zk_view_in_t in;
                memset(&in, 0, sizeof in);
                in.sheet_kind = sheets[vi];
                in.station_index = sheets[vi] == ZK_SHEET_STOP_STATION ? 2 : 0;
                in.minutes = 5;
                in.stop_phase = ZK_STOP_IDLE;
                in.soil_cfg = &cfg;
                in.sched_index = 0;
                zk_view_build(&src, &in, view);
                add_view(view);
            }
            for (vi = 0; vi < (int)(sizeof phases / sizeof phases[0]); vi++) {
                zk_view_in_t in;
                memset(&in, 0, sizeof in);
                in.sheet_kind = ZK_SHEET_NONE;
                in.minutes = 5;
                in.stop_phase = phases[vi];
                in.soil_cfg = &cfg;
                zk_view_build(&src, &in, view);
                add_view(view);
            }
            {
                zk_view_in_t in;
                memset(&in, 0, sizeof in);
                in.stop_phase = ZK_STOP_QUEUED;
                in.soil_cfg = &cfg;
                src.unreachable = 1;
                zk_view_build(&src, &in, view);
                add_view(view);
                src.unreachable = 0;
                in.stop_phase = ZK_STOP_IDLE;
                in.sched_index = 2;
                in.sheet_kind = ZK_SHEET_STOP_STATION;
                in.station_index = 2;
                zk_view_build(&src, &in, view);
                add_view(view);
            }
            free(kbuf);
            free(sbuf);
            free(soilbuf);
            free(hbuf);
        }
        closedir(d);
    }
    TCHECK(g_nuniq > 10, "glyph strings %d", g_nuniq);
    for (fi = 0; fi < sizeof fonts / sizeof fonts[0]; fi++) {
        int s;
        for (s = 0; s < g_nuniq; s++) {
            const char *p = g_uniq[s];
            while (*p) {
                uint32_t cp = 0;
                int n = take_cp(p, &cp);
                lv_font_glyph_dsc_t dsc;
                if (n < 0) {
                    bad++;
                    if (noted < 5) {
                        TCHECK(0, "bad utf8 in [%s]", g_uniq[s]);
                        noted++;
                    }
                    break;
                }
                if (n == 0) {
                    break;
                }
                if (cp != '\n' && cp != '\r') {
                    memset(&dsc, 0, sizeof dsc);
                    if (!lv_font_get_glyph_dsc(fonts[fi].font, &dsc, cp, 0) || dsc.is_placeholder) {
                        bad++;
                        if (noted < 5) {
                            TCHECK(0, "U+%04X missing from %s in [%.48s]", cp, fonts[fi].name, g_uniq[s]);
                            noted++;
                        }
                    }
                }
                p += n;
            }
        }
    }
    TCHECK(bad == 0, "glyph misses %d across %d strings", bad, g_nuniq);
    free(kiosk);
    free(sched);
    free(soil);
    free(lasts);
    free(view);
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
    test_names_24();
    test_sched_longname();
    test_sched_paused_long();
    test_sched_w_stress();
    test_wide_words();
    test_running_icon();
    test_glyphs();
    zk_data_stop();
    lv_deinit();
    rc = zk_test_report("test_ui_text");
    return rc;
}
