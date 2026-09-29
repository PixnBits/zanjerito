#include "zk_test.h"

#include "data.h"
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
    TCHECK(name->y_rel == 52, "info_name y_rel %d", name->y_rel);
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
        TEQ_S(name->text, "Morning cycle \xC2\xB7 Daily 8:23 AM");
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
        "live parity: alpha 2, beta 3, gamma 3 (bash cron 08:23) \xC2\xB7 Daily 8:23 AM \xC2\xB7 Daily 8:23 AM";
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
        snprintf(id, sizeof id, "tile_title%d", i);
        make_title(full, i + 1);
        t = find_box(b, n, id);
        TCHECK(t != NULL, "missing %s", id);
        if (t) {
            TCHECK(prefix_ellipsis(t->text, full), "%s text %s", id, t->text);
        }
    }
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
    test_running_long();
    zk_data_stop();
    lv_deinit();
    rc = zk_test_report("test_ui_text");
    return rc;
}
