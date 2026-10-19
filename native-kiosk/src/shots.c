#include "shots.h"

#include "data.h"
#include "platform.h"
#include "ui.h"
#include "zk_flow.h"
#include "zk_layout.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *folder;
    const char *file;
    int screen;
    int pressed;
    int chip;
    int stale;
    int needs_update;
    int stop_phase; /* -1 clears the hold */
    int minutes;    /* < 1 clears */
    int sched_i;    /* < 0 clears */
} shot_t;

/* Direction C names from the handoff. Example fixtures only.
 * minutes and sched_i are applied after set_view, which clears those holds. */
static const shot_t k_shots[] = {
    {"dc-idle-4", "C-idle-4.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-idle-5", "C-idle-5.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-idle-6", "C-idle-6.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-watering-4", "C-watering-4.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-watering-4-longname", "C-watering-4-longname.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-watering-4", "C-stop-slow-ack.png", ZK_SCREEN_HOME, 0, -1, 0, 0, ZK_STOP_QUEUED, 0, -1},
    {"dc-watering-4", "C-stop-no-reply.png", ZK_SCREEN_HOME, 0, -1, 0, 0, ZK_STOP_QUEUED, 0, -1},
    {"dc-watering-4", "C-stop-still-watering.png", ZK_SCREEN_HOME, 0, -1, 0, 0, ZK_STOP_WAIT, 0, -1},
    {"dc-watering-6", "C-watering-6.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-paused-4", "C-paused-4.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-paused-6", "C-paused-6.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-nothing-4", "C-nothing-4.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-nothing-6", "C-nothing-6.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-overlap-6", "C-overlap-6.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-overlap-6", "S5-overlap-detail.png", ZK_SCREEN_SHEET_OVERLAP, 0, -1, 0, 0, -1, 0, -1},
    {"dc-offline-6", "C-offline-6.png", ZK_SCREEN_HOME, 0, -1, 1, 0, -1, 0, -1},
    {"dc-offline-6", "C-offline-stop-1-confirm.png", ZK_SCREEN_SHEET_STOP_ALL, 0, -1, 1, 0, -1, 0, -1},
    {"dc-offline-6", "C-offline-stop-2-pending.png", ZK_SCREEN_HOME, 0, -1, 1, 0, ZK_STOP_QUEUED, 0, -1},
    {"dc-stopped", "C-offline-stop-3-stopped.png", ZK_SCREEN_HOME, 0, -1, 0, 0, ZK_STOP_CONFIRMED, 0, -1},
    {"dc-idle-6", "S1-confirm-run.png", ZK_SCREEN_SHEET_RUN, 0, 2, 0, 0, -1, 0, -1},
    {"dc-idle-6", "S1-confirm-run-min.png", ZK_SCREEN_SHEET_RUN, 0, 2, 0, 0, -1, 1, -1},
    {"dc-paused-6", "S2-confirm-anyway.png", ZK_SCREEN_SHEET_ANYWAY, 0, 0, 0, 0, -1, 0, -1},
    {"dc-watering-6", "S3-running-tile.png", ZK_SCREEN_SHEET_STOP_STATION, 0, 2, 0, 0, -1, 0, -1},
    {"dc-watering-6-last", "S3-running-tile-last.png", ZK_SCREEN_SHEET_STOP_STATION, 0, 3, 0, 0, -1, 0, -1},
    {"dc-manual-4", "S3-running-tile-manual.png", ZK_SCREEN_SHEET_STOP_STATION, 0, 0, 0, 0, -1, 0, -1},
    {"dc-watering-6", "S4-confirm-stop-all.png", ZK_SCREEN_SHEET_STOP_ALL, 0, -1, 0, 0, -1, 0, -1},
    {"dc-schedules-4", "C-schedules-stop-all.png", ZK_SCREEN_SHEET_STOP_ALL, 0, -1, 0, 0, -1, 0, -1},
    {"dc-soil-4", "C-soil-mix-4.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-soil-6", "C-soil-mix-6.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-soil-watering-6", "C-soil-watering-6.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-manual-4", "C-manual-run-4.png", ZK_SCREEN_HOME, 0, -1, 0, 0, -1, 0, -1},
    {"dc-schedules-1", "C-schedules-1.png", ZK_SCREEN_SCHEDULES, 0, -1, 0, 0, -1, 0, -1},
    {"dc-schedules-3", "C-schedules-3-p1.png", ZK_SCREEN_SCHEDULES, 0, -1, 0, 0, -1, 0, 0},
    {"dc-schedules-3", "C-schedules-3-p2.png", ZK_SCREEN_SCHEDULES, 0, -1, 0, 0, -1, 0, 2},
    {"dc-schedules-4", "C-schedules-4.png", ZK_SCREEN_SCHEDULES, 0, -1, 0, 0, -1, 0, -1},
    {"dc-schedules-6", "C-schedules-6.png", ZK_SCREEN_SCHEDULES, 0, -1, 0, 0, -1, 0, -1},
};

static int join2(char *dst, size_t cap, const char *a, const char *b)
{
    int n;
    if (!a || !b) {
        return -1;
    }
    n = snprintf(dst, cap, "%s/%s", a, b);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return 0;
}

static int one_shot(const char *root, const char *outdir, const shot_t *sc)
{
    char fix[768];
    char out[768];
    zk_app_t app;
    struct stat st;

    if (join2(fix, sizeof fix, root, sc->folder) != 0 || join2(out, sizeof out, outdir, sc->file) != 0) {
        fprintf(stderr, "shots: path too long for %s\n", sc->file);
        return -1;
    }
    if (stat(fix, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "shots: %s: not a directory\n", fix);
        return -1;
    }
    zk_data_stop();
    memset(&app, 0, sizeof app);
    app.fixture_dir = fix;
    app.live_clock = 0;
    zk_ui_set_fixture(fix);
    if (zk_data_start(&app) != 0) {
        fprintf(stderr, "shots: data start failed for %s\n", fix);
        return -1;
    }
    zk_data_debug_force_stale(sc->stale);
    zk_data_debug_force_needs_update(sc->needs_update);
    zk_ui_debug_set_view(sc->screen, sc->pressed, sc->chip);
    zk_ui_debug_set_minutes(sc->minutes);
    zk_ui_debug_set_sched(sc->sched_i);
    zk_ui_debug_set_stop(sc->stop_phase);
    zk_ui_refresh();
    if (zk_snapshot_png(out) != 0) {
        fprintf(stderr, "shots: write failed %s\n", out);
        return -1;
    }
    zk_ui_debug_set_stop(-1);
    zk_data_debug_force_stale(0);
    zk_data_debug_force_needs_update(0);
    return 0;
}

static int shot_named(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof k_shots / sizeof k_shots[0]; i++) {
        if (strcmp(k_shots[i].file, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Drop PNGs in outdir that this table no longer writes. Only after every shot succeeds. */
static void drop_stale(const char *outdir)
{
    DIR *d;
    struct dirent *ent;
    d = opendir(outdir);
    if (!d) {
        return;
    }
    while ((ent = readdir(d)) != NULL) {
        char path[768];
        size_t n = strlen(ent->d_name);
        if (n < 5 || strcmp(ent->d_name + n - 4, ".png") != 0) {
            continue;
        }
        if (shot_named(ent->d_name)) {
            continue;
        }
        if (join2(path, sizeof path, outdir, ent->d_name) != 0) {
            continue;
        }
        unlink(path);
    }
    closedir(d);
}

int zk_shots_run(const char *fixtures_root, const char *outdir)
{
    size_t i;
    if (!fixtures_root || !fixtures_root[0] || !outdir || !outdir[0]) {
        return -1;
    }
    if (mkdir(outdir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "shots: mkdir %s: %s\n", outdir, strerror(errno));
        return -1;
    }
    for (i = 0; i < sizeof k_shots / sizeof k_shots[0]; i++) {
        if (one_shot(fixtures_root, outdir, &k_shots[i]) != 0) {
            return -1;
        }
    }
    drop_stale(outdir);
    return 0;
}
