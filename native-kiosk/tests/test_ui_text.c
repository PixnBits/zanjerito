#include "zk_test.h"

#include "data.h"
#include "platform.h"
#include "ui.h"
#include "zk_copy.h"
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
        }
        TCHECK(zk_ui_debug_tile(i, &tile) == 1, "tile %d", i);
        TEQ_I(tile.on, 0);
        TEQ_I(tile.bg, 0xFCF5E6);
        TEQ_I(tile.border_w, 2);
        TEQ_I(tile.border, 0xD9C3A0);
        TEQ_I(tile.drop_visible, 0);
        TEQ_I(tile.title_w, tile.tile_w - 28);
    }
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
        TEQ_S(head->text, ms);
        TCHECK(!ends_ellipsis(head->text), "headline ellipsized");
    }
    if (time) {
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
        TEQ_S(head->text, cur);
        TCHECK(!ends_ellipsis(head->text), "headline ellipsized");
    }
    if (time) {
        TEQ_S(time->text, "3 min left");
    }
    if (sub) {
        TCHECK(strstr(sub->text, "next") != NULL, "secondary %s", sub->text);
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
            TEQ_S(slot->text, "Rate not measured");
            TCHECK(strstr(slot->text, "0%") == NULL, "stale showed a percent");
        }
    }
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
    test_home_title_width();
    test_running_long();
    test_sheets_soil_offline();
    zk_data_stop();
    lv_deinit();
    rc = zk_test_report("test_ui_text");
    return rc;
}
