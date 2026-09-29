#include "shots.h"

#include "data.h"
#include "platform.h"
#include "ui.h"
#include "zk_layout.h"

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
} shot_t;

static const shot_t k_shots[] = {
    {"home-rain", "01-home-idle-rain.png", ZK_SCREEN_HOME, 0, -1, 0},
    {"home-norain", "01b-home-idle-no-rain.png", ZK_SCREEN_HOME, 0, -1, 0},
    {"home-nosoil", "01c-home-no-soil.png", ZK_SCREEN_HOME, 0, -1, 0},
    {"home-stale", "01d-home-soil-stale.png", ZK_SCREEN_HOME, 0, -1, 0},
    {"home-fault", "01e-home-fault.png", ZK_SCREEN_HOME, 0, -1, 0},
    {"running", "02-running.png", ZK_SCREEN_RUNNING, 0, -1, 0},
    {"paused-rain", "03-paused-rain.png", ZK_SCREEN_PAUSED, 0, -1, 0},
    {"paused-manual", "03b-paused-manual.png", ZK_SCREEN_PAUSED, 0, -1, 0},
    {"home-rain", "04-pause-picker.png", ZK_SCREEN_PICKER, 0, -1, 0},
    {"home-rain", "05-schedules.png", ZK_SCREEN_SCHEDULES, 0, -1, 0},
    {"home-rain", "06-confirm-stop.png", ZK_SCREEN_CONFIRM_STOP, 0, -1, 0},
    {"home-rain", "07-confirm-pause.png", ZK_SCREEN_CONFIRM_PAUSE, 0, 0, 0},
    {"home-rain", "08-home-stop-pressed.png", ZK_SCREEN_HOME, ZK_TARGET_STOP, -1, 0},
    {"home-rain", "09-offline.png", ZK_SCREEN_HOME, 0, -1, 1}
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
    zk_ui_debug_set_view(sc->screen, sc->pressed, sc->chip);
    zk_ui_refresh();
    if (zk_snapshot_png(out) != 0) {
        fprintf(stderr, "shots: write failed %s\n", out);
        return -1;
    }
    return 0;
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
    return 0;
}
