#include "zk_test.h"
#include "zk_power.h"

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
    TEQ_I(file_int("brightness"), 40);
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
    TEQ_I(file_int("brightness"), 255);
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
    zk_power_init(NULL, NULL);
    zk_power_tick(NULL, 0, NULL);
    TEQ_I(zk_power_touch_event(NULL, 0, 1, 1), ZK_POWER_PASS);
    zk_power_shutdown(NULL);
    return zk_test_report("test_power");
}
