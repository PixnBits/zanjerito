#include "zk_test.h"
#include "zk_power.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_dir[80];
static int g_blank_n;
static int g_blank_last;
static int g_blank_fail;
static int g_saved_err = -1;
static char g_err[4096];

static int stub_blank(void *ctx, int powerdown)
{
    (void)ctx;
    g_blank_n++;
    g_blank_last = powerdown;
    if (g_blank_fail) {
        return -1;
    }
    return 0;
}

static void path_of(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", g_dir, name);
}

static void put_file(const char *name, const char *text)
{
    char path[128];
    FILE *f;

    path_of(path, sizeof path, name);
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "put %s: %s\n", path, strerror(errno));
        return;
    }
    fputs(text, f);
    fclose(f);
}

static int file_int(const char *name)
{
    char path[128];
    char buf[64];
    FILE *f;

    path_of(path, sizeof path, name);
    f = fopen(path, "r");
    if (!f) {
        return -999;
    }
    if (!fgets(buf, sizeof buf, f)) {
        fclose(f);
        return -999;
    }
    fclose(f);
    return atoi(buf);
}

static void reset_tree(void)
{
    char path[128];

    path_of(path, sizeof path, "brightness");
    rmdir(path);
    unlink(path);
    put_file("brightness", "200\n");
    put_file("actual_brightness", "200\n");
    put_file("max_brightness", "255\n");
    put_file("bl_power", "0\n");
    g_blank_n = 0;
    g_blank_last = -1;
    g_blank_fail = 0;
}

static void boot_dir(zk_power_t *p, int dim, int off, int level, const char *dir, int fb_real)
{
    zk_power_config_t c;

    memset(&c, 0, sizeof c);
    c.dim_after_sec = dim;
    c.off_after_sec = off;
    c.dim_level = level;
    c.backlight_dir = dir;
    c.fb_real = fb_real;
    c.blank = stub_blank;
    zk_power_init(p, &c);
}

static void boot(zk_power_t *p, int dim, int off, int level)
{
    boot_dir(p, dim, off, level, g_dir, 0);
}

static void cap_on(void)
{
    char path[128];
    int fd;

    snprintf(path, sizeof path, "%s/stderr.txt", g_dir);
    fflush(stderr);
    g_saved_err = dup(fileno(stderr));
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        dup2(fd, fileno(stderr));
        close(fd);
    }
    g_err[0] = '\0';
}

static void cap_off(void)
{
    char path[128];
    int fd;
    ssize_t n;

    fflush(stderr);
    if (g_saved_err >= 0) {
        dup2(g_saved_err, fileno(stderr));
        close(g_saved_err);
        g_saved_err = -1;
    }
    snprintf(path, sizeof path, "%s/stderr.txt", g_dir);
    g_err[0] = '\0';
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, g_err, sizeof g_err - 1);
        if (n < 0) {
            n = 0;
        }
        g_err[n] = '\0';
        close(fd);
    }
}

static int count_power_lines(void)
{
    int n = 0;
    const char *p = g_err;

    while ((p = strstr(p, "power:")) != NULL) {
        n++;
        p += 6;
    }
    return n;
}

static void expect_trace(const zk_power_t *p, int index, const char *what, const char *value)
{
    const char *got_w = "";
    const char *got_v = "";

    TCHECK(zk_power_trace_at(p, index, &got_w, &got_v), "trace %d missing", index);
    if (!got_w) {
        got_w = "";
    }
    if (!got_v) {
        got_v = "";
    }
    TEQ_S(got_w, what);
    TEQ_S(got_v, value);
}

static zk_power_inputs_t flagged(const char *name)
{
    zk_power_inputs_t in;

    memset(&in, 0, sizeof in);
    if (strcmp(name, "watering") == 0) {
        in.watering = 1;
    } else if (strcmp(name, "lockout") == 0) {
        in.lockout = 1;
    } else if (strcmp(name, "fault") == 0) {
        in.fault = 1;
    } else if (strcmp(name, "unreachable") == 0) {
        in.unreachable = 1;
    } else if (strcmp(name, "modal") == 0) {
        in.modal_open = 1;
    } else if (strcmp(name, "paused") == 0) {
        in.paused = 1;
    }
    return in;
}

static void test_thresholds(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};

    reset_tree();
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 1999, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 200);
    TEQ_I(zk_power_trace_len(&p), 0);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 40);
    TEQ_I(file_int("bl_power"), 0);
    zk_power_tick(&p, 4999, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 40);
    zk_power_tick(&p, 5000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_OFF);
    TEQ_I(file_int("brightness"), 0);
    TEQ_I(file_int("bl_power"), 1);
    TEQ_I(g_blank_n, 0);
    TEQ_I(file_int("actual_brightness"), 200);
    TEQ_I(zk_power_trace_len(&p), 3);
    expect_trace(&p, 0, "brightness", "40");
    expect_trace(&p, 1, "brightness", "0");
    expect_trace(&p, 2, "bl_power", "1");
}

static void test_touch_resets(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};
    int i;

    reset_tree();
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &in);
    for (i = 1; i <= 10; i++) {
        TEQ_I(zk_power_touch_event(&p, i * 100, 0, 0), ZK_POWER_PASS);
    }
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);

    reset_tree();
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 1999, &in);
    TEQ_I(zk_power_touch_event(&p, 1999, 1, 0), ZK_POWER_PASS);
    TEQ_I(zk_power_touch_event(&p, 2049, 0, 0), ZK_POWER_PASS);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 200);
    /* Release is the last contact sample, so both timers start there. */
    zk_power_tick(&p, 2049 + 1999, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    zk_power_tick(&p, 2049 + 2000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    zk_power_tick(&p, 2049 + 4999, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    zk_power_tick(&p, 2049 + 5000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_OFF);
}

static void test_off_wake_swallow(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};

    reset_tree();
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    zk_power_tick(&p, 5000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_OFF);
    TEQ_I(zk_power_touch_event(&p, 5000, 1, 1), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 200);
    TEQ_I(file_int("bl_power"), 0);
    TEQ_I(zk_power_touch_event(&p, 5100, 1, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_touch_event(&p, 5200, 0, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_touch_event(&p, 5499, 1, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_touch_event(&p, 5499, 0, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_touch_event(&p, 5500, 1, 0), ZK_POWER_PASS);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(zk_power_trace_len(&p), 5);
    expect_trace(&p, 3, "bl_power", "0");
    expect_trace(&p, 4, "brightness", "200");
}

static void test_dimmed_stop_and_other(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};

    reset_tree();
    boot(&p, 2, 30, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_touch_event(&p, 2000, 1, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 200);
    TEQ_I(zk_power_touch_event(&p, 2050, 0, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_touch_event(&p, 2349, 1, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_touch_event(&p, 2349, 0, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_touch_event(&p, 2350, 1, 0), ZK_POWER_PASS);

    reset_tree();
    boot(&p, 2, 30, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_touch_event(&p, 2000, 1, 1), ZK_POWER_PASS);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 200);
    TEQ_I(zk_power_touch_event(&p, 2010, 0, 1), ZK_POWER_PASS);
    TEQ_I(zk_power_touch_event(&p, 2020, 1, 1), ZK_POWER_PASS);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
}

static void test_blockers(void)
{
    static const char *names[] = {"watering", "lockout", "fault", "unreachable", "modal"};
    int i;

    for (i = 0; i < 5; i++) {
        zk_power_t p;
        zk_power_inputs_t blocked = flagged(names[i]);
        zk_power_inputs_t clear = {0};

        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &blocked);
        zk_power_tick(&p, 100000, &blocked);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        TEQ_I(file_int("brightness"), 200);
        TEQ_I(zk_power_trace_len(&p), 0);
        TEQ_I(file_int("bl_power"), 0);

        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &clear);
        zk_power_tick(&p, 2000, &clear);
        TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
        zk_power_tick(&p, 2500, &blocked);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        TEQ_I(file_int("brightness"), 200);
        TEQ_I(file_int("bl_power"), 0);

        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &clear);
        zk_power_tick(&p, 5000, &clear);
        TEQ_I(zk_power_state(&p), ZK_POWER_OFF);
        zk_power_tick(&p, 5000, &blocked);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        TEQ_I(file_int("brightness"), 200);
        TEQ_I(file_int("bl_power"), 0);

        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &blocked);
        zk_power_tick(&p, 100000, &blocked);
        zk_power_tick(&p, 100000, &clear);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        zk_power_tick(&p, 100000 + 1999, &clear);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        zk_power_tick(&p, 100000 + 2000, &clear);
        TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
        TEQ_I(file_int("actual_brightness"), 200);
    }
}

static void test_paused_and_run_start(void)
{
    zk_power_t p;
    zk_power_inputs_t paused = flagged("paused");
    zk_power_inputs_t clear = {0};
    zk_power_inputs_t water = flagged("watering");

    reset_tree();
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &paused);
    zk_power_tick(&p, 5000, &paused);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 40);
    TEQ_I(file_int("bl_power"), 0);
    zk_power_tick(&p, 100000, &paused);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 40);
    TEQ_I(file_int("bl_power"), 0);

    reset_tree();
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &clear);
    zk_power_tick(&p, 2000, &clear);
    zk_power_tick(&p, 5000, &clear);
    TEQ_I(zk_power_state(&p), ZK_POWER_OFF);
    zk_power_tick(&p, 5000, &paused);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 40);
    TEQ_I(file_int("bl_power"), 0);
    expect_trace(&p, 3, "bl_power", "0");
    expect_trace(&p, 4, "brightness", "40");

    reset_tree();
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &clear);
    zk_power_tick(&p, 2000, &clear);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    zk_power_tick(&p, 2100, &water);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 200);
}

static void test_shutdown_restore(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};
    int n;

    reset_tree();
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 5000, &in);
    zk_power_shutdown(&p);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 200);
    TEQ_I(file_int("bl_power"), 0);
    n = zk_power_trace_len(&p);
    put_file("brightness", "3\n");
    zk_power_shutdown(&p);
    TEQ_I(file_int("brightness"), 3);
    TEQ_I(zk_power_trace_len(&p), n);

    reset_tree();
    put_file("brightness", "0\n");
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    /* Original 0 is already dark: dim does not write. Shutdown restores max. */
    TEQ_I(file_int("brightness"), 0);
    zk_power_shutdown(&p);
    TEQ_I(file_int("brightness"), 255);
    TEQ_I(file_int("bl_power"), 0);

    reset_tree();
    put_file("brightness", "0\n");
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_shutdown(&p);
    TEQ_I(file_int("brightness"), 0);
    TEQ_I(zk_power_trace_len(&p), 0);
}

static void test_missing_and_unwritable(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};
    char missing[128];
    char path[128];
    int n_dim;
    int n_off;
    int n_extra;
    int n_wake;
    int st_dim;
    int st_off;
    zk_power_touch_action_t act;

    snprintf(missing, sizeof missing, "%s/no-such", g_dir);
    g_blank_n = 0;
    cap_on();
    boot_dir(&p, 2, 5, 40, missing, 1);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    st_dim = (int)zk_power_state(&p);
    n_dim = g_blank_n;
    zk_power_tick(&p, 5000, &in);
    st_off = (int)zk_power_state(&p);
    n_off = g_blank_n;
    act = zk_power_touch_event(&p, 5000, 1, 0);
    n_wake = g_blank_n;
    cap_off();
    TEQ_I(st_dim, ZK_POWER_DIMMED);
    TEQ_I(n_dim, 0);
    TEQ_I(st_off, ZK_POWER_OFF);
    TEQ_I(n_off, 1);
    TEQ_I(g_blank_last, 0);
    TEQ_I(act, ZK_POWER_SWALLOW);
    TEQ_I(n_wake, 2);
    TEQ_I(count_power_lines(), 0);

    g_blank_n = 0;
    cap_on();
    boot_dir(&p, 2, 5, 40, missing, 0);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 5000, &in);
    st_off = (int)zk_power_state(&p);
    n_off = g_blank_n;
    cap_off();
    TEQ_I(st_off, ZK_POWER_OFF);
    TEQ_I(n_off, 0);
    TEQ_I(count_power_lines(), 0);

    reset_tree();
    cap_on();
    boot_dir(&p, 2, 5, 40, g_dir, 1);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 5000, &in);
    cap_off();
    TEQ_I(g_blank_n, 0);
    TEQ_I(file_int("brightness"), 0);
    TEQ_I(file_int("bl_power"), 1);
    TEQ_I(count_power_lines(), 0);

    reset_tree();
    path_of(path, sizeof path, "brightness");
    unlink(path);
    TCHECK(mkdir(path, 0755) == 0, "mkdir brightness");
    cap_on();
    boot_dir(&p, 2, 5, 40, g_dir, 1);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    st_dim = (int)zk_power_state(&p);
    n_dim = g_blank_n;
    zk_power_tick(&p, 5000, &in);
    st_off = (int)zk_power_state(&p);
    n_off = g_blank_n;
    zk_power_tick(&p, 6000, &in);
    zk_power_tick(&p, 7000, &in);
    n_extra = g_blank_n;
    act = zk_power_touch_event(&p, 7000, 1, 0);
    n_wake = g_blank_n;
    cap_off();
    TEQ_I(st_dim, ZK_POWER_DIMMED);
    TEQ_I(n_dim, 0);
    TEQ_I(st_off, ZK_POWER_OFF);
    TEQ_I(n_off, 1);
    TEQ_I(n_extra, 1);
    TEQ_I(act, ZK_POWER_SWALLOW);
    TEQ_I((int)zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(n_wake, 2);
    TEQ_I(g_blank_last, 0);
    TEQ_I(count_power_lines(), 1);
    TCHECK(strstr(g_err, "power: brightness not writable") != NULL, "unwritable log: %s", g_err);

    g_blank_n = 0;
    g_blank_fail = 1;
    cap_on();
    boot_dir(&p, 2, 5, 40, missing, 1);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 5000, &in);
    st_off = (int)zk_power_state(&p);
    n_off = g_blank_n;
    zk_power_tick(&p, 8000, &in);
    n_extra = g_blank_n;
    act = zk_power_touch_event(&p, 8000, 1, 0);
    n_wake = g_blank_n;
    cap_off();
    TEQ_I(st_off, ZK_POWER_OFF);
    TEQ_I(n_off, 1);
    TEQ_I(n_extra, 1);
    TEQ_I(act, ZK_POWER_SWALLOW);
    TEQ_I((int)zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(n_wake, 1);
    TEQ_I(count_power_lines(), 1);
    g_blank_fail = 0;
}

static void test_clamp_and_disable(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};
    zk_power_config_t c;

    reset_tree();
    cap_on();
    boot(&p, 2000000000, 50, 99999);
    cap_off();
    TEQ_I(zk_power_dim_after_sec(&p), 1000000000);
    TEQ_I(zk_power_off_after_sec(&p), 0);
    TEQ_I(zk_power_dim_level(&p), 255);
    TCHECK(strstr(g_err, "OFF disabled") != NULL, "clamp log: %s", g_err);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 1000000000LL * 1000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    /* Configured level clamps to 255, but that must not raise the saved 200. */
    TEQ_I(file_int("brightness"), 200);
    zk_power_tick(&p, 1000000000LL * 1000 + 100000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);

    reset_tree();
    cap_on();
    boot(&p, -4, 10, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 100000, &in);
    TEQ_I(zk_power_touch_event(&p, 100000, 1, 1), ZK_POWER_PASS);
    zk_power_shutdown(&p);
    cap_off();
    TEQ_I(zk_power_enabled(&p), 0);
    TEQ_I(file_int("brightness"), 200);
    TEQ_I(g_blank_n, 0);
    TEQ_I(count_power_lines(), 0);

    memset(&c, 0, sizeof c);
    c.dim_after_sec = 0;
    c.off_after_sec = 10;
    c.dim_level = 40;
    c.backlight_dir = NULL;
    c.fb_real = 1;
    c.blank = stub_blank;
    g_blank_n = 0;
    cap_on();
    zk_power_init(&p, &c);
    zk_power_tick(&p, 100000, &in);
    TEQ_I(zk_power_touch_event(&p, 1, 1, 1), ZK_POWER_PASS);
    zk_power_shutdown(&p);
    cap_off();
    TEQ_I(zk_power_enabled(&p), 0);
    TEQ_S(zk_power_backlight_dir(&p), ZK_POWER_DEFAULT_BACKLIGHT);
    TEQ_I(g_blank_n, 0);
    TEQ_I(count_power_lines(), 0);

    reset_tree();
    cap_on();
    boot(&p, 5, 5, 40);
    cap_off();
    TEQ_I(zk_power_off_after_sec(&p), 0);
    TCHECK(strstr(g_err, "OFF disabled") != NULL, "equal off log");
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 5000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    zk_power_tick(&p, 100000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);

    reset_tree();
    cap_on();
    boot(&p, 2, -3, 40);
    cap_off();
    TEQ_I(zk_power_off_after_sec(&p), 0);
    TEQ_I(count_power_lines(), 0);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 100000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);

    reset_tree();
    boot(&p, 2, 10, 0);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(file_int("brightness"), 1);

    reset_tree();
    boot(&p, 2, 10, -5);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(file_int("brightness"), 1);

    reset_tree();
    put_file("max_brightness", "30\n");
    boot(&p, 2, 10, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(file_int("brightness"), 30);
}

static void unlink_bl(void)
{
    char path[128];
    zk_power_t p;
    zk_power_inputs_t in = {0};

    reset_tree();
    path_of(path, sizeof path, "bl_power");
    unlink(path);
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    zk_power_tick(&p, 5000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_OFF);
    TEQ_I(file_int("brightness"), 0);
    TEQ_I(zk_power_trace_len(&p), 2);
    expect_trace(&p, 0, "brightness", "40");
    expect_trace(&p, 1, "brightness", "0");
}

static void test_never_brighten(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};

    reset_tree();
    put_file("brightness", "20\n");
    boot(&p, 2, 30, 51);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 20);
    expect_trace(&p, 0, "brightness", "20");
    TEQ_I(zk_power_touch_event(&p, 2000, 1, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 20);
    zk_power_shutdown(&p);
    TEQ_I(file_int("brightness"), 20);

    reset_tree();
    put_file("brightness", "255\n");
    boot(&p, 2, 30, 51);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 51);
    expect_trace(&p, 0, "brightness", "51");

    reset_tree();
    put_file("brightness", "0\n");
    put_file("max_brightness", "180\n");
    cap_on();
    boot(&p, 2, 30, 51);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 0);
    TEQ_I(zk_power_trace_len(&p), 0);
    TEQ_I(zk_power_touch_event(&p, 2100, 1, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 180);
    cap_off();
    TEQ_I(count_power_lines(), 0);

    reset_tree();
    put_file("brightness", "0\n");
    put_file("max_brightness", "180\n");
    cap_on();
    boot(&p, 2, 30, 51);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(file_int("brightness"), 0);
    zk_power_shutdown(&p);
    cap_off();
    TEQ_I(file_int("brightness"), 180);
    TEQ_I(file_int("bl_power"), 0);
    TEQ_I(count_power_lines(), 0);

    reset_tree();
    put_file("brightness", "51\n");
    boot(&p, 2, 30, 51);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 51);
    expect_trace(&p, 0, "brightness", "51");

    reset_tree();
    put_file("brightness", "40\n");
    boot(&p, 2, 30, 0);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(file_int("brightness"), 1);
    expect_trace(&p, 0, "brightness", "1");
}

static void bad_max_keeps_orig(const char *text, int drop_max)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};
    char path[128];

    reset_tree();
    put_file("brightness", "40\n");
    if (drop_max) {
        path_of(path, sizeof path, "max_brightness");
        unlink(path);
    } else {
        put_file("max_brightness", text);
    }
    cap_on();
    boot(&p, 2, 30, 51);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    cap_off();
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 40);
    TEQ_I(count_power_lines(), 0);
}

static void test_bad_max(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};
    char path[128];

    bad_max_keeps_orig(NULL, 1);
    bad_max_keeps_orig("nope\n", 0);
    bad_max_keeps_orig("99999999999\n", 0);
    bad_max_keeps_orig("0\n", 0);
    bad_max_keeps_orig("-3\n", 0);

    reset_tree();
    put_file("brightness", "nope\n");
    path_of(path, sizeof path, "max_brightness");
    unlink(path);
    cap_on();
    boot(&p, 2, 30, 51);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(file_int("brightness"), 51);
    zk_power_shutdown(&p);
    cap_off();
    TEQ_I(file_int("brightness"), 255);
    TEQ_I(count_power_lines(), 0);
}

static void test_unknown_original(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};
    char path[128];

    reset_tree();
    put_file("brightness", "nope\n");
    put_file("max_brightness", "180\n");
    cap_on();
    boot(&p, 2, 30, 51);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 51);
    zk_power_shutdown(&p);
    cap_off();
    TEQ_I(file_int("brightness"), 180);
    TEQ_I(count_power_lines(), 0);

    reset_tree();
    path_of(path, sizeof path, "brightness");
    unlink(path);
    put_file("max_brightness", "180\n");
    cap_on();
    boot(&p, 2, 30, 51);
    zk_power_shutdown(&p);
    cap_off();
    TEQ_I(file_int("brightness"), -999);
    TEQ_I(zk_power_trace_len(&p), 0);
    TEQ_I(count_power_lines(), 0);

    cap_on();
    boot(&p, 2, 30, 51);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(file_int("brightness"), 51);
    zk_power_shutdown(&p);
    cap_off();
    TEQ_I(file_int("brightness"), 180);
    TEQ_I(count_power_lines(), 0);
}

static void test_init_saves_original(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};

    reset_tree();
    put_file("brightness", "180\n");
    boot(&p, 2, 30, 51);
    put_file("brightness", "7\n");
    TEQ_I(zk_power_trace_len(&p), 0);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 51);
    TEQ_I(zk_power_touch_event(&p, 2000, 1, 0), ZK_POWER_SWALLOW);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 180);
    zk_power_shutdown(&p);
    TEQ_I(file_int("brightness"), 180);

    reset_tree();
    put_file("brightness", "180\n");
    boot(&p, 2, 30, 51);
    put_file("brightness", "7\n");
    zk_power_shutdown(&p);
    TEQ_I(file_int("brightness"), 7);
    TEQ_I(zk_power_trace_len(&p), 0);
    TEQ_I((int)zk_power_state(&p), ZK_POWER_ACTIVE);
}

static void test_chmod_unwritable(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};
    char path[128];
    int st_dim;
    int st_off;
    zk_power_touch_action_t act;

    if (geteuid() == 0) {
        return;
    }
    reset_tree();
    path_of(path, sizeof path, "brightness");
    TCHECK(chmod(path, 0444) == 0, "chmod 0444 brightness");
    g_blank_n = 0;
    cap_on();
    boot_dir(&p, 2, 5, 40, g_dir, 1);
    zk_power_tick(&p, 0, &in);
    zk_power_tick(&p, 2000, &in);
    st_dim = (int)zk_power_state(&p);
    zk_power_tick(&p, 5000, &in);
    st_off = (int)zk_power_state(&p);
    act = zk_power_touch_event(&p, 5000, 1, 0);
    cap_off();
    chmod(path, 0644);
    TEQ_I(st_dim, ZK_POWER_DIMMED);
    TEQ_I(st_off, ZK_POWER_OFF);
    TEQ_I(act, ZK_POWER_SWALLOW);
    TEQ_I((int)zk_power_state(&p), ZK_POWER_ACTIVE);
    TEQ_I(file_int("brightness"), 200);
    TEQ_I(g_blank_n, 2);
    TEQ_I(g_blank_last, 0);
    TCHECK(count_power_lines() >= 1, "chmod write should log, got: %s", g_err);
}

static int dir_count(const char *dir)
{
    DIR *d;
    struct dirent *de;
    int n = 0;

    d = opendir(dir);
    if (!d) {
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        n++;
    }
    closedir(d);
    return n;
}

static void expect_times(const struct stat *a, const struct stat *b, const char *what)
{
    TCHECK(a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec,
           "%s mtime changed", what);
    TCHECK(a->st_atim.tv_sec == b->st_atim.tv_sec && a->st_atim.tv_nsec == b->st_atim.tv_nsec,
           "%s atime changed", what);
}

static void test_disabled_does_not_touch(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};
    char sub[160];
    char file[180];
    char missing[160];
    char dark[160];
    char buf[64];
    struct stat file_before;
    struct stat file_after;
    struct stat dir_before;
    struct stat dir_after;
    struct stat st;
    struct timespec ts[2];
    int fd;
    ssize_t n;

    snprintf(sub, sizeof sub, "%s/untouched", g_dir);
    snprintf(file, sizeof file, "%s/sentinel", sub);
    snprintf(missing, sizeof missing, "%s/no-such-backlight", g_dir);
    TCHECK(mkdir(sub, 0755) == 0, "mkdir sentinel dir: %s", strerror(errno));
    fd = open(file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    TCHECK(fd >= 0, "create sentinel: %s", strerror(errno));
    if (fd >= 0) {
        n = write(fd, "stay\n", 5);
        TCHECK(n == 5, "write sentinel");
        close(fd);
    }
    ts[0].tv_sec = 100000;
    ts[0].tv_nsec = 0;
    ts[1] = ts[0];
    TCHECK(utimensat(AT_FDCWD, file, ts, 0) == 0, "stamp sentinel");
    TCHECK(utimensat(AT_FDCWD, sub, ts, 0) == 0, "stamp sentinel dir");
    TCHECK(stat(file, &file_before) == 0, "stat sentinel before");
    TCHECK(stat(sub, &dir_before) == 0, "stat dir before");

    g_blank_n = 0;
    cap_on();
    boot_dir(&p, 0, 10, 51, sub, 1);
    zk_power_tick(&p, 5000, &in);
    TEQ_I(zk_power_touch_event(&p, 5000, 1, 1), ZK_POWER_PASS);
    zk_power_shutdown(&p);
    cap_off();
    TCHECK(stat(file, &file_after) == 0, "stat sentinel after");
    TCHECK(stat(sub, &dir_after) == 0, "stat dir after");
    expect_times(&file_before, &file_after, "sentinel");
    expect_times(&dir_before, &dir_after, "sentinel dir");
    TEQ_I(dir_count(sub), 1);
    fd = open(file, O_RDONLY | O_CLOEXEC);
    TCHECK(fd >= 0, "reopen sentinel");
    buf[0] = '\0';
    if (fd >= 0) {
        n = read(fd, buf, sizeof buf - 1);
        if (n < 0) {
            n = 0;
        }
        buf[n] = '\0';
        close(fd);
    }
    TEQ_S(buf, "stay\n");
    TEQ_I(zk_power_enabled(&p), 0);
    TEQ_I(zk_power_trace_len(&p), 0);
    TEQ_I(g_blank_n, 0);
    TEQ_I(count_power_lines(), 0);

    g_blank_n = 0;
    cap_on();
    boot_dir(&p, 0, 10, 51, missing, 1);
    zk_power_tick(&p, 5000, &in);
    TEQ_I(zk_power_touch_event(&p, 1, 1, 0), ZK_POWER_PASS);
    zk_power_shutdown(&p);
    cap_off();
    TCHECK(stat(missing, &st) != 0, "disabled init created %s", missing);
    TEQ_I(zk_power_enabled(&p), 0);
    TEQ_I(zk_power_trace_len(&p), 0);
    TEQ_I(count_power_lines(), 0);
    TEQ_I(g_blank_n, 0);

    if (geteuid() == 0) {
        return;
    }
    snprintf(dark, sizeof dark, "%s/dark", g_dir);
    if (mkdir(dark, 0) != 0) {
        TCHECK(0, "mkdir dark: %s", strerror(errno));
        return;
    }
    g_blank_n = 0;
    cap_on();
    boot_dir(&p, 0, 10, 51, dark, 1);
    zk_power_tick(&p, 8000, &in);
    TEQ_I(zk_power_touch_event(&p, 8000, 1, 0), ZK_POWER_PASS);
    zk_power_shutdown(&p);
    cap_off();
    chmod(dark, 0755);
    TEQ_I(zk_power_enabled(&p), 0);
    TEQ_I(zk_power_trace_len(&p), 0);
    TEQ_I(count_power_lines(), 0);
    TEQ_I(g_blank_n, 0);
}

/* Press-edge latch: the first sample of a contact decides swallow vs pass. */
typedef struct {
    int64_t ms;
    int pressed;
    int hit_is_stop;
    zk_power_touch_action_t want;
} touch_row_t;

static void run_touch_rows(zk_power_t *p, const touch_row_t *rows, int n, zk_power_state_t want_state)
{
    int i;

    for (i = 0; i < n; i++) {
        TEQ_I(zk_power_touch_event(p, rows[i].ms, rows[i].pressed, rows[i].hit_is_stop), rows[i].want);
        TEQ_I(zk_power_state(p), want_state);
    }
}

static void test_contact_latch(void)
{
    zk_power_t p;
    zk_power_inputs_t in = {0};

    /* DIMMED: press off STOP, slide onto STOP, release. Whole contact swallowed. */
    {
        static const touch_row_t rows[] = {
            {2000, 1, 0, ZK_POWER_SWALLOW},
            {2100, 1, 1, ZK_POWER_SWALLOW},
            {2200, 0, 1, ZK_POWER_SWALLOW},
            {2499, 1, 0, ZK_POWER_SWALLOW},
            {2499, 0, 0, ZK_POWER_SWALLOW},
            {2500, 1, 0, ZK_POWER_PASS},
        };
        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &in);
        zk_power_tick(&p, 2000, &in);
        TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
        run_touch_rows(&p, rows, (int)(sizeof rows / sizeof rows[0]), ZK_POWER_ACTIVE);
        TEQ_I(file_int("brightness"), 200);
        TEQ_I(file_int("bl_power"), 0);
    }

    /* OFF: press anywhere, slide onto STOP, release. Whole contact swallowed. */
    {
        static const touch_row_t rows[] = {
            {5000, 1, 0, ZK_POWER_SWALLOW},
            {5100, 1, 1, ZK_POWER_SWALLOW},
            {5200, 0, 1, ZK_POWER_SWALLOW},
            {5499, 1, 0, ZK_POWER_SWALLOW},
            {5499, 0, 0, ZK_POWER_SWALLOW},
            {5500, 1, 0, ZK_POWER_PASS},
        };
        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &in);
        zk_power_tick(&p, 2000, &in);
        zk_power_tick(&p, 5000, &in);
        TEQ_I(zk_power_state(&p), ZK_POWER_OFF);
        run_touch_rows(&p, rows, (int)(sizeof rows / sizeof rows[0]), ZK_POWER_ACTIVE);
        TEQ_I(file_int("brightness"), 200);
        TEQ_I(file_int("bl_power"), 0);
    }

    /* DIMMED: press on STOP, slide off STOP, release. Latched pass, no guard. */
    {
        static const touch_row_t rows[] = {
            {2000, 1, 1, ZK_POWER_PASS},
            {2100, 1, 0, ZK_POWER_PASS},
            {2200, 0, 0, ZK_POWER_PASS},
            {2201, 1, 0, ZK_POWER_PASS},
        };
        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &in);
        zk_power_tick(&p, 2000, &in);
        TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
        run_touch_rows(&p, rows, (int)(sizeof rows / sizeof rows[0]), ZK_POWER_ACTIVE);
        TEQ_I(file_int("brightness"), 200);
        TEQ_I(file_int("bl_power"), 0);
    }

    /* ACTIVE: slides pass throughout. */
    {
        static const touch_row_t rows[] = {
            {10, 1, 0, ZK_POWER_PASS},
            {20, 1, 1, ZK_POWER_PASS},
            {30, 0, 1, ZK_POWER_PASS},
            {40, 1, 1, ZK_POWER_PASS},
            {50, 1, 0, ZK_POWER_PASS},
            {60, 0, 0, ZK_POWER_PASS},
        };
        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &in);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        run_touch_rows(&p, rows, (int)(sizeof rows / sizeof rows[0]), ZK_POWER_ACTIVE);
        TEQ_I(file_int("brightness"), 200);
        TEQ_I(zk_power_trace_len(&p), 0);
    }
}

static void test_paused_with_blockers(void)
{
    static const char *names[] = {"watering", "lockout", "fault", "unreachable", "modal"};
    zk_power_t p;
    zk_power_inputs_t paused = flagged("paused");
    int i;

    reset_tree();
    boot(&p, 2, 5, 40);
    zk_power_tick(&p, 0, &paused);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    zk_power_tick(&p, 1999, &paused);
    TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
    zk_power_tick(&p, 2000, &paused);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("brightness"), 40);
    zk_power_tick(&p, 100000, &paused);
    TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
    TEQ_I(file_int("bl_power"), 0);

    for (i = 0; i < 5; i++) {
        zk_power_inputs_t both = flagged(names[i]);
        zk_power_inputs_t only_paused = flagged("paused");

        both.paused = 1;

        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &both);
        zk_power_tick(&p, 100000, &both);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        TEQ_I(file_int("brightness"), 200);
        TEQ_I(file_int("bl_power"), 0);
        TEQ_I(zk_power_trace_len(&p), 0);

        zk_power_tick(&p, 100000, &only_paused);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        zk_power_tick(&p, 100000 + 1999, &only_paused);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        zk_power_tick(&p, 100000 + 2000, &only_paused);
        TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
        TEQ_I(file_int("brightness"), 40);
        zk_power_tick(&p, 100000 + 100000, &only_paused);
        TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
        TEQ_I(file_int("bl_power"), 0);

        reset_tree();
        boot(&p, 2, 5, 40);
        zk_power_tick(&p, 0, &only_paused);
        zk_power_tick(&p, 2000, &only_paused);
        TEQ_I(zk_power_state(&p), ZK_POWER_DIMMED);
        zk_power_tick(&p, 2500, &both);
        TEQ_I(zk_power_state(&p), ZK_POWER_ACTIVE);
        TEQ_I(file_int("brightness"), 200);
        TEQ_I(file_int("bl_power"), 0);
    }
}

static void bl_mkdirp(const char *path)
{
    TCHECK(mkdir(path, 0755) == 0, "mkdir %s: %s", path, strerror(errno));
}

static void bl_put_at(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");

    TCHECK(f != NULL, "open %s: %s", path, strerror(errno));
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void bl_dev(const char *root, const char *name, int with_br, int with_bl)
{
    char path[256];

    snprintf(path, sizeof path, "%s/%s", root, name);
    bl_mkdirp(path);
    if (with_br) {
        snprintf(path, sizeof path, "%s/%s/brightness", root, name);
        bl_put_at(path, "10\n");
    }
    if (with_bl) {
        snprintf(path, sizeof path, "%s/%s/bl_power", root, name);
        bl_put_at(path, "0\n");
    }
}

static void test_resolve_backlight(void)
{
    char root[160];
    char out[ZK_POWER_DIR_MAX];
    char expect[256];
    char full[256];
    char longn[400];
    int src;

    snprintf(root, sizeof root, "%s/blroot", g_dir);
    bl_mkdirp(root);

    /* Override by name wins over rpi_backlight preference. */
    {
        char r[180];
        snprintf(r, sizeof r, "%s/ovname", root);
        bl_mkdirp(r);
        bl_dev(r, "rpi_backlight", 1, 1);
        bl_dev(r, "10-0045", 1, 1);
        src = zk_power_resolve_backlight(r, "10-0045", out, sizeof out);
        TEQ_I(src, ZK_POWER_BL_OVERRIDE);
        snprintf(expect, sizeof expect, "%s/10-0045", r);
        TEQ_S(out, expect);
    }

    /* Override by full path wins. */
    {
        char r[180];
        snprintf(r, sizeof r, "%s/ovpath", root);
        bl_mkdirp(r);
        bl_dev(r, "rpi_backlight", 1, 1);
        bl_dev(r, "10-0045", 1, 1);
        snprintf(full, sizeof full, "%s/10-0045", r);
        src = zk_power_resolve_backlight(r, full, out, sizeof out);
        TEQ_I(src, ZK_POWER_BL_OVERRIDE);
        TEQ_S(out, full);
    }

    /* Invalid override (missing, ".", "..", too long) falls back to auto. */
    {
        char r[180];
        snprintf(r, sizeof r, "%s/ovbad", root);
        bl_mkdirp(r);
        bl_dev(r, "10-0045", 1, 1);
        snprintf(expect, sizeof expect, "%s/10-0045", r);

        cap_on();
        src = zk_power_resolve_backlight(r, "no-such", out, sizeof out);
        cap_off();
        TEQ_I(src, ZK_POWER_BL_10_0045);
        TEQ_S(out, expect);
        TCHECK(strstr(g_err, "no-such") != NULL, "invalid override names the value");

        cap_on();
        src = zk_power_resolve_backlight(r, ".", out, sizeof out);
        cap_off();
        TEQ_I(src, ZK_POWER_BL_10_0045);
        TCHECK(strstr(g_err, "ZAN_BACKLIGHT: invalid") != NULL, "dot name is invalid");

        cap_on();
        src = zk_power_resolve_backlight(r, "..", out, sizeof out);
        cap_off();
        TEQ_I(src, ZK_POWER_BL_10_0045);

        memset(longn, 'a', sizeof longn - 1);
        longn[sizeof longn - 1] = '\0';
        cap_on();
        src = zk_power_resolve_backlight(r, longn, out, sizeof out);
        cap_off();
        TEQ_I(src, ZK_POWER_BL_10_0045);
        TCHECK(strstr(g_err, "falling back") != NULL, "too-long override falls back");
    }

    /* rpi_backlight preferred when both rpi_backlight and 10-0045 exist. */
    {
        char r[180];
        snprintf(r, sizeof r, "%s/prefer", root);
        bl_mkdirp(r);
        bl_dev(r, "10-0045", 1, 1);
        bl_dev(r, "rpi_backlight", 1, 1);
        src = zk_power_resolve_backlight(r, NULL, out, sizeof out);
        TEQ_I(src, ZK_POWER_BL_RPI);
        snprintf(expect, sizeof expect, "%s/rpi_backlight", r);
        TEQ_S(out, expect);
    }

    /* 10-0045 found when it is the only preferred device. */
    {
        char r[180];
        snprintf(r, sizeof r, "%s/dsi", root);
        bl_mkdirp(r);
        bl_dev(r, "10-0045", 0, 0);
        bl_dev(r, "aaa", 1, 1);
        src = zk_power_resolve_backlight(r, NULL, out, sizeof out);
        TEQ_I(src, ZK_POWER_BL_10_0045);
        snprintf(expect, sizeof expect, "%s/10-0045", r);
        TEQ_S(out, expect);
    }

    /* Sorted scan picks the first directory with writable brightness/bl_power. */
    {
        char r[180];
        snprintf(r, sizeof r, "%s/scan", root);
        bl_mkdirp(r);
        bl_dev(r, "aaa", 0, 0);
        bl_dev(r, "mmm", 1, 0);
        bl_dev(r, "zzz", 1, 1);
        src = zk_power_resolve_backlight(r, NULL, out, sizeof out);
        TEQ_I(src, ZK_POWER_BL_SCAN);
        snprintf(expect, sizeof expect, "%s/mmm", r);
        TEQ_S(out, expect);
    }

    /* Scan skips a file that is not a directory. */
    {
        char r[180];
        char file[200];
        snprintf(r, sizeof r, "%s/scanfile", root);
        bl_mkdirp(r);
        snprintf(file, sizeof file, "%s/aaa", r);
        bl_put_at(file, "not a dir\n");
        bl_dev(r, "bbb", 0, 1);
        src = zk_power_resolve_backlight(r, NULL, out, sizeof out);
        TEQ_I(src, ZK_POWER_BL_SCAN);
        snprintf(expect, sizeof expect, "%s/bbb", r);
        TEQ_S(out, expect);
    }

    /* Empty root: none. */
    {
        char r[180];
        snprintf(r, sizeof r, "%s/empty", root);
        bl_mkdirp(r);
        out[0] = 'x';
        src = zk_power_resolve_backlight(r, NULL, out, sizeof out);
        TEQ_I(src, ZK_POWER_BL_NONE);
        TEQ_S(out, "");
    }

    /* Missing root: none. */
    {
        char r[180];
        snprintf(r, sizeof r, "%s/missing-root", root);
        src = zk_power_resolve_backlight(r, NULL, out, sizeof out);
        TEQ_I(src, ZK_POWER_BL_NONE);
        TEQ_S(out, "");
    }

    /* Empty override string is treated as unset. */
    {
        char r[180];
        snprintf(r, sizeof r, "%s/emptyov", root);
        bl_mkdirp(r);
        bl_dev(r, "rpi_backlight", 1, 1);
        src = zk_power_resolve_backlight(r, "", out, sizeof out);
        TEQ_I(src, ZK_POWER_BL_RPI);
    }
}

int main(void)
{
    char tmpl[] = "/tmp/zkpowXXXXXX";
    char *dir;

    dir = mkdtemp(tmpl);
    if (!dir) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    test_thresholds();
    test_touch_resets();
    test_off_wake_swallow();
    test_dimmed_stop_and_other();
    test_blockers();
    test_paused_and_run_start();
    test_shutdown_restore();
    test_missing_and_unwritable();
    test_clamp_and_disable();
    unlink_bl();
    test_never_brighten();
    test_bad_max();
    test_unknown_original();
    test_init_saves_original();
    test_chmod_unwritable();
    test_disabled_does_not_touch();
    test_contact_latch();
    test_paused_with_blockers();
    test_resolve_backlight();
    zk_power_init(NULL, NULL);
    zk_power_tick(NULL, 0, NULL);
    TEQ_I(zk_power_touch_event(NULL, 0, 1, 1), ZK_POWER_PASS);
    zk_power_shutdown(NULL);
    return zk_test_report("test_power");
}
