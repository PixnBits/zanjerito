#define _GNU_SOURCE

#include "app.h"
#include "data.h"
#include "platform.h"
#include "shots.h"
#include "ui.h"
#include "zk_console.h"
#include "zk_power.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    OPT_TOUCH_SWAP = 256,
    OPT_TOUCH_FLIP_X,
    OPT_TOUCH_FLIP_Y,
    OPT_SHOT_ALL,
    OPT_FIXTURES_ROOT,
    OPT_DIM_AFTER,
    OPT_OFF_AFTER,
    OPT_DIM_LEVEL,
    OPT_BACKLIGHT
};

enum {
    ACT_WAIT = 1,
    ACT_TAP,
    ACT_SHOT,
    ACT_MAX = 128,
    TAP_HOLD_MS = 50
};

typedef struct {
    int kind;
    int x;
    int y;
    int ms;
    char path[768];
} act_t;

static act_t g_acts[ACT_MAX];
static int g_nacts;
static int g_act_i;
static int g_act_started;
static double g_act_t0;
static int g_script_fail;
static volatile sig_atomic_t g_stop;
static zk_power_t g_power;
static int g_power_on;

static void power_shutdown_now(void)
{
    if (!g_power_on) {
        return;
    }
    zk_power_shutdown(&g_power);
    zk_platform_set_power_transitions(zk_power_transitions(&g_power));
    zk_platform_bind_power(NULL);
    g_power_on = 0;
}

static void usage(FILE *fp, const char *argv0)
{
    fprintf(fp,
            "usage: %s [options]\n"
            "  --api URL            API base URL (else env ZAN_API)\n"
            "  --fixture DIR        fixture directory; API not required\n"
            "  --live-clock         follow the system clock\n"
            "  --allow-writes       permit mutating API calls\n"
            "  --fb PATH            open this framebuffer only (never a default device)\n"
            "  --touch PATH         evdev device; default is autodetect\n"
            "  --touch-swap         swap touch X/Y after open\n"
            "  --touch-flip-x       flip touch X\n"
            "  --touch-flip-y       flip touch Y\n"
            "  --script SPEC        tap:X,Y;wait:MS;shot:FILE;... on a memory display\n"
            "  --shot NAME=FILE     render the current screen once to FILE\n"
            "  --shot-all OUTDIR    call zk_shots_run (needs --fixtures-root)\n"
            "  --fixtures-root DIR  fixture tree passed to zk_shots_run\n"
            "  --duration SEC       exit after SEC seconds from process start\n"
            "  --dim-after SEC      idle seconds before dim (default 120; 0 disables dim and off)\n"
            "  --off-after SEC      idle seconds from last touch before off (default 600; 0 disables off)\n"
            "  --dim-level N        dim brightness 0-255 (default 51; written value is at least 1)\n"
            "  --backlight DIR      sysfs backlight directory\n"
            "  --stats              print one JSON stats line on stderr\n"
            "  --help               show this help\n"
            "Display power runs only with --fb, unless ZK_POWER_FORCE=1 (test-only).\n"
            "Env fallbacks when the flag is omitted: ZAN_DIM_AFTER_SEC, ZAN_OFF_AFTER_SEC,\n"
            "ZAN_DIM_LEVEL, ZAN_BACKLIGHT. Flags win.\n",
            argv0);
}

static int parse_nonneg(const char *s, int *out)
{
    char *end = NULL;
    long v;

    if (!s || !s[0]) {
        return -1;
    }
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end || v < 0 || v > 1000000000L) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

/* 1 if set, 0 if unset, -1 if invalid. */
static int take_env_nonneg(const char *name, int *out)
{
    const char *s = getenv(name);

    if (!s || !s[0]) {
        return 0;
    }
    if (parse_nonneg(s, out) != 0) {
        fprintf(stderr, "%s: invalid\n", name);
        return -1;
    }
    return 1;
}

static int take_env_level(const char *name, int *out)
{
    int rc = take_env_nonneg(name, out);

    if (rc > 0 && *out > 255) {
        fprintf(stderr, "%s: invalid\n", name);
        return -1;
    }
    return rc;
}

static int take_env_backlight(const char **out)
{
    const char *s = getenv("ZAN_BACKLIGHT");

    if (!s || !s[0]) {
        return 0;
    }
    if (strlen(s) >= ZK_POWER_DIR_MAX) {
        fprintf(stderr, "ZAN_BACKLIGHT: invalid\n");
        return -1;
    }
    *out = s;
    return 1;
}

static int dir_ok(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return 0;
    }
    return 1;
}

static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static int add_act(const act_t *a)
{
    if (g_nacts >= ACT_MAX) {
        fprintf(stderr, "script: too many commands\n");
        return -1;
    }
    g_acts[g_nacts++] = *a;
    return 0;
}

static int parse_one(const char *tok)
{
    act_t a;
    const char *arg;

    memset(&a, 0, sizeof a);
    tok = skip_ws(tok);
    if (strncmp(tok, "tap:", 4) == 0) {
        if (sscanf(tok + 4, "%d,%d", &a.x, &a.y) != 2) {
            return -1;
        }
        a.kind = ACT_TAP;
        return add_act(&a);
    }
    if (strncmp(tok, "wait:", 5) == 0) {
        if (parse_nonneg(skip_ws(tok + 5), &a.ms) != 0) {
            return -1;
        }
        a.kind = ACT_WAIT;
        return add_act(&a);
    }
    if (strncmp(tok, "shot:", 5) == 0) {
        arg = skip_ws(tok + 5);
        if (!arg[0] || strlen(arg) >= sizeof a.path) {
            return -1;
        }
        memcpy(a.path, arg, strlen(arg) + 1);
        a.kind = ACT_SHOT;
        return add_act(&a);
    }
    return -1;
}

static int parse_script(const char *spec)
{
    char *buf;
    char *cursor;
    char *semi;

    if (!spec) {
        return -1;
    }
    buf = strdup(spec);
    if (!buf) {
        return -1;
    }
    cursor = buf;
    while (cursor) {
        char *tok;
        semi = strchr(cursor, ';');
        if (semi) {
            *semi = '\0';
        }
        tok = cursor;
        if (skip_ws(tok)[0]) {
            if (parse_one(tok) != 0) {
                free(buf);
                return -1;
            }
        }
        cursor = semi ? semi + 1 : NULL;
    }
    free(buf);
    return 0;
}

static void script_begin(double now)
{
    act_t *a = &g_acts[g_act_i];
    g_act_t0 = now;
    g_act_started = 1;
    if (a->kind == ACT_TAP) {
        zk_platform_pointer(a->x, a->y, 1);
        zk_platform_note_injected_touch();
    } else if (a->kind == ACT_SHOT) {
        if (zk_snapshot_png(a->path) != 0) {
            g_script_fail = 1;
        }
    }
}

static void script_tick(double now)
{
    if (g_nacts == 0) {
        return;
    }
    while (g_act_i < g_nacts) {
        act_t *a = &g_acts[g_act_i];
        double elapsed;

        if (!g_act_started) {
            script_begin(now);
            if (a->kind == ACT_SHOT || (a->kind == ACT_WAIT && a->ms <= 0)) {
                g_act_i++;
                g_act_started = 0;
                continue;
            }
        }
        elapsed = (now - g_act_t0) * 1000.0;
        if (a->kind == ACT_WAIT) {
            if (elapsed + 0.5 >= (double)a->ms) {
                g_act_i++;
                g_act_started = 0;
                continue;
            }
            return;
        }
        if (a->kind == ACT_TAP) {
            /* 1 = pressed, 2 = released and waiting for one handler pass. */
            if (g_act_started == 1 && elapsed >= (double)TAP_HOLD_MS) {
                zk_platform_pointer(a->x, a->y, 0);
                g_act_started = 2;
                return;
            }
            if (g_act_started == 2) {
                g_act_i++;
                g_act_started = 0;
                continue;
            }
            return;
        }
        g_act_i++;
        g_act_started = 0;
    }
}

/* Milliseconds until the current script step is due, or -1 if idle. */
static int script_timeout_ms(double now)
{
    act_t *a;
    double elapsed;
    double need;
    double left;

    if (g_act_i >= g_nacts) {
        return -1;
    }
    if (!g_act_started || g_act_started == 2) {
        return 0;
    }
    a = &g_acts[g_act_i];
    if (a->kind == ACT_SHOT) {
        return 0;
    }
    elapsed = (now - g_act_t0) * 1000.0;
    need = a->kind == ACT_WAIT ? (double)a->ms : (double)TAP_HOLD_MS;
    left = need - elapsed;
    if (left <= 0) {
        return 0;
    }
    if (left > 600000) {
        left = 600000;
    }
    return (int)(left + 0.999);
}

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
    zk_platform_wake();
}

static int split_shot(const char *spec, const char **name, const char **file)
{
    const char *eq;

    if (!spec) {
        return -1;
    }
    eq = strchr(spec, '=');
    if (!eq || eq == spec || eq[1] == '\0') {
        return -1;
    }
    *name = spec;
    *file = eq + 1;
    return 0;
}

int main(int argc, char **argv)
{
    static const struct option long_opts[] = {
        {"api", required_argument, 0, 'a'},
        {"fixture", required_argument, 0, 'f'},
        {"live-clock", no_argument, 0, 'L'},
        {"allow-writes", no_argument, 0, 'w'},
        {"fb", required_argument, 0, 'b'},
        {"touch", required_argument, 0, 't'},
        {"touch-swap", no_argument, 0, OPT_TOUCH_SWAP},
        {"touch-flip-x", no_argument, 0, OPT_TOUCH_FLIP_X},
        {"touch-flip-y", no_argument, 0, OPT_TOUCH_FLIP_Y},
        {"script", required_argument, 0, 's'},
        {"shot", required_argument, 0, 'S'},
        {"shot-all", required_argument, 0, OPT_SHOT_ALL},
        {"fixtures-root", required_argument, 0, OPT_FIXTURES_ROOT},
        {"duration", required_argument, 0, 'd'},
        {"dim-after", required_argument, 0, OPT_DIM_AFTER},
        {"off-after", required_argument, 0, OPT_OFF_AFTER},
        {"dim-level", required_argument, 0, OPT_DIM_LEVEL},
        {"backlight", required_argument, 0, OPT_BACKLIGHT},
        {"stats", no_argument, 0, 'T'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    const char *api = NULL;
    const char *fixture = NULL;
    const char *fb = NULL;
    const char *touch = NULL;
    const char *script = NULL;
    const char *shot_spec = NULL;
    const char *shot_name = NULL;
    const char *shot_file = NULL;
    const char *shot_all = NULL;
    const char *fixtures_root = NULL;
    int live_clock = 0;
    int allow_writes = 0;
    int touch_swap = 0;
    int touch_flip_x = 0;
    int touch_flip_y = 0;
    int stats = 0;
    int duration = -1;
    int memory_only = 0;
    int dim_after = -1;
    int off_after = -1;
    int dim_level = -1;
    int saw_dim = 0;
    int saw_off = 0;
    int saw_level = 0;
    int saw_bl = 0;
    int want_power = 0;
    int env_rc;
    const char *backlight = NULL;
    const char *force;
    int rc = 0;
    int data_on = 0;
    zk_app_t app;
    zk_platform_opts_t opts;

    zk_platform_mark_start();

    for (;;) {
        int c = getopt_long(argc, argv, "", long_opts, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'a':
            api = optarg;
            break;
        case 'f':
            fixture = optarg;
            break;
        case 'L':
            live_clock = 1;
            break;
        case 'w':
            allow_writes = 1;
            break;
        case 'b':
            fb = optarg;
            break;
        case 't':
            touch = optarg;
            break;
        case OPT_TOUCH_SWAP:
            touch_swap = 1;
            break;
        case OPT_TOUCH_FLIP_X:
            touch_flip_x = 1;
            break;
        case OPT_TOUCH_FLIP_Y:
            touch_flip_y = 1;
            break;
        case 's':
            script = optarg;
            break;
        case 'S':
            shot_spec = optarg;
            break;
        case OPT_SHOT_ALL:
            shot_all = optarg;
            break;
        case OPT_FIXTURES_ROOT:
            fixtures_root = optarg;
            break;
        case 'd':
            if (parse_nonneg(optarg, &duration) != 0) {
                usage(stderr, argv[0]);
                return 2;
            }
            break;
        case OPT_DIM_AFTER:
            if (parse_nonneg(optarg, &dim_after) != 0) {
                usage(stderr, argv[0]);
                return 2;
            }
            saw_dim = 1;
            break;
        case OPT_OFF_AFTER:
            if (parse_nonneg(optarg, &off_after) != 0) {
                usage(stderr, argv[0]);
                return 2;
            }
            saw_off = 1;
            break;
        case OPT_DIM_LEVEL:
            if (parse_nonneg(optarg, &dim_level) != 0 || dim_level > 255) {
                usage(stderr, argv[0]);
                return 2;
            }
            saw_level = 1;
            break;
        case OPT_BACKLIGHT:
            if (!optarg[0] || strlen(optarg) >= ZK_POWER_DIR_MAX) {
                usage(stderr, argv[0]);
                return 2;
            }
            backlight = optarg;
            saw_bl = 1;
            break;
        case 'T':
            stats = 1;
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 0;
        default:
            usage(stderr, argv[0]);
            return 2;
        }
    }
    if (!saw_dim) {
        env_rc = take_env_nonneg("ZAN_DIM_AFTER_SEC", &dim_after);
        if (env_rc < 0) {
            usage(stderr, argv[0]);
            return 2;
        }
        if (env_rc > 0) {
            saw_dim = 1;
        }
    }
    if (!saw_off) {
        env_rc = take_env_nonneg("ZAN_OFF_AFTER_SEC", &off_after);
        if (env_rc < 0) {
            usage(stderr, argv[0]);
            return 2;
        }
        if (env_rc > 0) {
            saw_off = 1;
        }
    }
    if (!saw_level) {
        env_rc = take_env_level("ZAN_DIM_LEVEL", &dim_level);
        if (env_rc < 0) {
            usage(stderr, argv[0]);
            return 2;
        }
        if (env_rc > 0) {
            saw_level = 1;
        }
    }
    if (!saw_bl) {
        env_rc = take_env_backlight(&backlight);
        if (env_rc < 0) {
            usage(stderr, argv[0]);
            return 2;
        }
    }
    if (!saw_dim) {
        dim_after = ZK_POWER_DEFAULT_DIM_AFTER_SEC;
    }
    if (!saw_off) {
        off_after = ZK_POWER_DEFAULT_OFF_AFTER_SEC;
    }
    if (!saw_level) {
        dim_level = ZK_POWER_DEFAULT_DIM_LEVEL;
    }
    if (!backlight || !backlight[0]) {
        backlight = ZK_POWER_DEFAULT_BACKLIGHT;
    }
    if (optind < argc) {
        fprintf(stderr, "unexpected argument: %s\n", argv[optind]);
        usage(stderr, argv[0]);
        return 2;
    }
    if (!api) {
        api = getenv("ZAN_API");
    }
    if (api && !api[0]) {
        api = NULL;
    }
    if (shot_spec && split_shot(shot_spec, &shot_name, &shot_file) != 0) {
        fprintf(stderr, "shot: expected NAME=FILE\n");
        usage(stderr, argv[0]);
        return 2;
    }
    if (shot_name && !shot_name[0]) {
        usage(stderr, argv[0]);
        return 2;
    }
    if (shot_all && (!shot_all[0] || !fixtures_root || !fixtures_root[0])) {
        fprintf(stderr, "shot-all: need OUTDIR and --fixtures-root DIR\n");
        usage(stderr, argv[0]);
        return 2;
    }
    if (!api && !fixture && !fixtures_root) {
        fprintf(stderr, "need --api URL (or ZAN_API) or --fixture DIR\n");
        usage(stderr, argv[0]);
        return 2;
    }
    if (fixture && !dir_ok(fixture)) {
        fprintf(stderr, "fixture: %s: not a directory\n", fixture);
        return 1;
    }
    if (fixtures_root && !dir_ok(fixtures_root)) {
        fprintf(stderr, "fixtures-root: %s: not a directory\n", fixtures_root);
        return 1;
    }
    if (script && parse_script(script) != 0) {
        fprintf(stderr, "script: could not parse\n");
        usage(stderr, argv[0]);
        return 2;
    }

    /* Script and one-shot renders stay off the framebuffer. */
    memory_only = script || shot_file || shot_all;
    if (fb && memory_only) {
        fprintf(stderr, "display: memory backend; not opening framebuffer\n");
        fb = NULL;
    }
    force = getenv("ZK_POWER_FORCE");
    want_power = (fb && fb[0]) || (force && strcmp(force, "1") == 0);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    memset(&opts, 0, sizeof opts);
    opts.fb_path = fb;
    opts.touch_path = touch;
    opts.touch_swap = touch_swap;
    opts.touch_flip_x = touch_flip_x;
    opts.touch_flip_y = touch_flip_y;
    opts.virtual_pointer = script ? 1 : 0;
    opts.autodetect_touch = (!script && !shot_file && !shot_all && !touch) ? 1 : 0;
    if (zk_platform_init(&opts) != 0) {
        zk_platform_print_stats(stats);
        return 1;
    }
    if (want_power) {
        zk_power_config_t pc;

        memset(&pc, 0, sizeof pc);
        pc.dim_after_sec = dim_after;
        pc.off_after_sec = off_after;
        pc.dim_level = dim_level;
        pc.backlight_dir = backlight;
        pc.fb_real = zk_platform_fb_is_real();
        if (pc.fb_real) {
            pc.blank = zk_platform_fbio_blank;
        }
        zk_power_init(&g_power, &pc);
        zk_platform_bind_power(&g_power);
        g_power_on = 1;
    }
    if (fb) {
        zk_console_cursor(1);
    }

    memset(&app, 0, sizeof app);
    app.api = api;
    app.fixture_dir = fixture;
    app.allow_writes = allow_writes;
    app.live_clock = live_clock;
    zk_ui_init(zk_platform_display(), &app);

    if (shot_all) {
        rc = zk_shots_run(fixtures_root, shot_all);
        goto out;
    }
    if (shot_file && !script) {
        rc = zk_snapshot_png(shot_file);
        goto out;
    }
    if ((fixture && fixture[0]) || (api && api[0])) {
        if (zk_data_start(&app) != 0) {
            fprintf(stderr, "data: start failed\n");
            rc = 1;
            goto out;
        }
        data_on = 1;
    }

    while (!g_stop) {
        double now = zk_platform_mono();
        uint32_t wait;
        uint32_t cap;
        int script_ms;
        int busy;

        if (duration >= 0 && zk_platform_since_main() >= (double)duration) {
            break;
        }
        script_tick(now);
        zk_ui_tick();
        if (g_power_on) {
            zk_power_inputs_t pin;

            zk_ui_power_inputs(&pin);
            zk_power_tick(&g_power, zk_platform_mono_ms(), &pin);
        }
        if (g_stop) {
            break;
        }
        wait = zk_platform_handle_timers();
        busy = zk_platform_busy();
        cap = busy ? 16u : 100u;
        if (wait == LV_NO_TIMER_READY) {
            wait = 100u;
        }
        if (wait > cap) {
            wait = cap;
        }
        /* A 0 timeout makes poll() spin. That was the idle-loop bug. */
        if (wait == 0) {
            wait = 1u;
        }
        script_ms = script_timeout_ms(zk_platform_mono());
        if (script_ms >= 0 && (uint32_t)script_ms < wait) {
            wait = (uint32_t)script_ms;
            if (wait == 0) {
                wait = 1u;
            }
        }
        if (duration >= 0) {
            double left = (double)duration - zk_platform_since_main();
            if (left <= 0) {
                break;
            }
            if (left < (double)wait / 1000.0) {
                uint32_t dms = (uint32_t)(left * 1000.0);
                wait = dms == 0 ? 1u : dms;
            }
        }
        zk_platform_wait(wait);
    }

    rc = g_script_fail ? 1 : 0;
out:
    if (data_on) {
        zk_data_stop();
    }
    power_shutdown_now();
    if (fb) {
        zk_console_cursor(0);
    }
    zk_platform_print_stats(stats);
    return rc == 0 ? 0 : 1;
}
