#define _GNU_SOURCE

#include "platform.h"

#include "ui.h"
#include "zk_fb.h"
#include "zk_layout.h"
#include "zk_png.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

#include <linux/fb.h>
#include <linux/input.h>

enum {
    SNAP_W = ZK_SCREEN_W,
    SNAP_H = ZK_SCREEN_H,
    PENDING_CAP = 256,
    SAMPLE_MAX = 262144
};

typedef struct {
    int64_t sec;
    int64_t usec;
} touch_stamp_t;

static uint8_t g_frame[SNAP_W * SNAP_H * 4] __attribute__((aligned(64)));
static uint8_t g_snap[SNAP_W * SNAP_H * 4] __attribute__((aligned(64)));

static lv_display_t *g_disp;
static lv_indev_t *g_virt;
static lv_point_t g_pt;
static lv_indev_state_t g_ptr_state = LV_INDEV_STATE_RELEASED;
static lv_indev_read_cb_t g_evdev_orig;
static int g_evdev_down;
static int g_virt_down;
static zk_power_t *g_power;
static char g_fb_path[384];
static int g_fb_real;
static int g_fb_fd = -1;
static uint8_t *g_fb_map;
static size_t g_fb_map_len;
static unsigned g_fb_xres;
static unsigned g_fb_yres;
static unsigned g_fb_px_bytes;
static zk_fb_geom_t g_fb_geom;
static uint32_t g_fb_src_stride;
static uint8_t *g_fb_draw;
static int g_power_transitions;

static int g_tfd = -1;
static int g_wake_r = -1;
static int g_wake_w = -1;
static int g_have_frame;

static double g_main_mono;
static double g_proc_mono;
static int g_got_first;
static double g_first_ms;
static double g_first_main_ms;
static uint64_t g_frames;

static double *g_render;
static int g_render_n;
static int g_render_cap;

static double *g_lat;
static int g_lat_n;
static int g_lat_cap;
static uint64_t g_touch_events;

static touch_stamp_t g_pending[PENDING_CAP];
static int g_pending_n;

double zk_platform_mono(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int64_t zk_platform_mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + (int64_t)t.tv_nsec / 1000000;
}

static void gate_pointer(lv_indev_data_t *data, int *down)
{
    int pressed;
    int hit;

    if (!g_power || !zk_power_enabled(g_power)) {
        return;
    }
    pressed = data->state == LV_INDEV_STATE_PRESSED ? 1 : 0;
    if (!pressed && !*down) {
        return;
    }
    hit = 0;
    if (pressed && !*down) {
        hit = zk_ui_hit_is_stop(data->point.x, data->point.y);
    }
    if (zk_power_touch_event(g_power, zk_platform_mono_ms(), pressed, hit) == ZK_POWER_SWALLOW) {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    *down = pressed;
}

static uint32_t tick_cb(void)
{
    struct timespec t;
    uint64_t ms;
    clock_gettime(CLOCK_MONOTONIC, &t);
    ms = (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
    return (uint32_t)ms;
}

static int read_proc_stat(unsigned long *utime, unsigned long *stime, unsigned long *starttime)
{
    FILE *f = fopen("/proc/self/stat", "r");
    char buf[4096];
    char *rp;
    int n;

    if (!f) {
        return -1;
    }
    if (!fgets(buf, sizeof buf, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    rp = strrchr(buf, ')');
    if (!rp || rp[1] == '\0') {
        return -1;
    }
    /* fields after comm: state.. utime(14) stime(15) starttime(22) */
    n = sscanf(rp + 2,
               "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %lu",
               utime, stime, starttime);
    return n == 3 ? 0 : -1;
}

void zk_platform_mark_start(void)
{
    struct timespec mono;
    double up = 0;
    unsigned long ut = 0;
    unsigned long st = 0;
    unsigned long start = 0;
    long hz;
    FILE *f;

    clock_gettime(CLOCK_MONOTONIC, &mono);
    g_main_mono = (double)mono.tv_sec + (double)mono.tv_nsec / 1e9;
    f = fopen("/proc/uptime", "r");
    if (f) {
        if (fscanf(f, "%lf", &up) != 1) {
            up = 0;
        }
        fclose(f);
    }
    hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) {
        hz = 100;
    }
    if (up > 0 && read_proc_stat(&ut, &st, &start) == 0) {
        double age = up - (double)start / (double)hz;
        if (age < 0) {
            age = 0;
        }
        g_proc_mono = g_main_mono - age;
    } else {
        g_proc_mono = g_main_mono;
    }
}

double zk_platform_since_main(void)
{
    return zk_platform_mono() - g_main_mono;
}

static void sample_add(double **v, int *n, int *cap, double x)
{
    if (*n >= SAMPLE_MAX) {
        return;
    }
    if (*n >= *cap) {
        int nc = *cap ? *cap * 2 : 128;
        double *p;
        if (nc > SAMPLE_MAX) {
            nc = SAMPLE_MAX;
        }
        p = realloc(*v, (size_t)nc * sizeof(double));
        if (!p) {
            return;
        }
        *v = p;
        *cap = nc;
    }
    (*v)[(*n)++] = x;
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

static double percentile_95(double *v, int n)
{
    double *tmp;
    double out;
    int idx;

    if (n <= 0) {
        return 0;
    }
    tmp = malloc((size_t)n * sizeof(double));
    if (!tmp) {
        return v[n - 1];
    }
    memcpy(tmp, v, (size_t)n * sizeof(double));
    qsort(tmp, (size_t)n, sizeof(double), cmp_double);
    idx = (n * 95 + 99) / 100 - 1;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= n) {
        idx = n - 1;
    }
    out = tmp[idx];
    free(tmp);
    return out;
}

static void summarize(double *v, int n, double *avg, double *p95, double *mx)
{
    int i;
    double sum = 0;
    double m = 0;

    for (i = 0; i < n; i++) {
        sum += v[i];
        if (v[i] > m) {
            m = v[i];
        }
    }
    *avg = n ? sum / (double)n : 0;
    *p95 = percentile_95(v, n);
    if (mx) {
        *mx = n ? m : 0;
    }
}

static void queue_touch(int64_t sec, int64_t usec)
{
    g_touch_events++;
    if (g_pending_n >= PENDING_CAP) {
        memmove(&g_pending[0], &g_pending[1], sizeof(g_pending[0]) * (PENDING_CAP - 1));
        g_pending_n = PENDING_CAP - 1;
    }
    g_pending[g_pending_n].sec = sec;
    g_pending[g_pending_n].usec = usec;
    g_pending_n++;
}

static int event_is_touch(const struct input_event *ev)
{
    if (ev->type == EV_KEY &&
        (ev->code == BTN_TOUCH || ev->code == BTN_LEFT || ev->code == BTN_TOOL_FINGER)) {
        return 1;
    }
    if (ev->type == EV_ABS &&
        (ev->code == ABS_X || ev->code == ABS_Y || ev->code == ABS_MT_POSITION_X ||
         ev->code == ABS_MT_POSITION_Y || ev->code == ABS_MT_TRACKING_ID)) {
        return 1;
    }
    return 0;
}

static void drain_touch_fd(void)
{
    struct input_event ev;

    if (g_tfd < 0) {
        return;
    }
    while (read(g_tfd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
        if (event_is_touch(&ev)) {
            queue_touch((int64_t)ev.time.tv_sec, (int64_t)ev.time.tv_usec);
        }
    }
}

static void settle_touch(void)
{
    struct timespec now;
    int i;

    if (g_pending_n <= 0) {
        return;
    }
    clock_gettime(CLOCK_REALTIME, &now);
    for (i = 0; i < g_pending_n; i++) {
        double ms = (double)(now.tv_sec - g_pending[i].sec) * 1000.0 +
                    (double)now.tv_nsec / 1e6 - (double)g_pending[i].usec / 1000.0;
        if (ms < 0) {
            ms = 0;
        }
        sample_add(&g_lat, &g_lat_n, &g_lat_cap, ms);
    }
    g_pending_n = 0;
}

static void capture(const uint8_t *src, uint32_t stride, int32_t w, int32_t h)
{
    int32_t y;
    int32_t rows;
    int32_t cols;

    if (!src || w <= 0 || h <= 0) {
        return;
    }
    cols = w < SNAP_W ? w : SNAP_W;
    rows = h < SNAP_H ? h : SNAP_H;
    if (stride < (uint32_t)cols * 4u) {
        return;
    }
    for (y = 0; y < rows; y++) {
        memcpy(g_snap + (size_t)y * SNAP_W * 4u, src + (size_t)y * stride, (size_t)cols * 4u);
    }
    g_have_frame = 1;
}

static void on_flush(lv_event_t *e)
{
    lv_display_t *disp;
    lv_draw_buf_t *db;

    if (lv_event_get_code(e) != LV_EVENT_FLUSH_FINISH) {
        return;
    }
    disp = lv_event_get_target(e);
    if (!lv_display_flush_is_last(disp)) {
        return;
    }
    db = lv_display_get_buf_active(disp);
    if (db && db->data) {
        capture(db->data, db->header.stride, (int32_t)db->header.w, (int32_t)db->header.h);
    }
    if (!g_got_first) {
        double now = zk_platform_mono();
        g_got_first = 1;
        g_first_ms = (now - g_proc_mono) * 1000.0;
        if (g_first_ms < 0) {
            g_first_ms = 0;
        }
        g_first_main_ms = (now - g_main_mono) * 1000.0;
        if (g_first_main_ms < 0) {
            g_first_main_ms = 0;
        }
    }
    g_frames++;
    settle_touch();
}

static void mem_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    (void)px;
    lv_display_flush_ready(disp);
}

static void fb_release(void)
{
    if (g_fb_map && g_fb_map != MAP_FAILED) {
        munmap(g_fb_map, g_fb_map_len);
    }
    g_fb_map = NULL;
    g_fb_map_len = 0;
    if (g_fb_fd >= 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
    }
    free(g_fb_draw);
    g_fb_draw = NULL;
}

/* Assumes LV_DISPLAY_RENDER_MODE_FULL: the draw buffer is a full frame, so
 * source rows are addressed at the absolute y/x of the area with the
 * draw-buffer stride. Switching to PARTIAL or DIRECT render mode requires
 * changing the source offset math. */
static void fb_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    int32_t x1;
    int32_t x2;
    int32_t y1;
    int32_t y2;
    int32_t y;
    uint32_t stride;
    unsigned bpp;
    lv_draw_buf_t *db;

    if (!g_fb_map || !area || !px || g_fb_px_bytes == 0) {
        lv_display_flush_ready(disp);
        return;
    }
    bpp = g_fb_px_bytes;
    stride = g_fb_src_stride;
    db = lv_display_get_buf_active(disp);
    if (db && db->header.stride > 0) {
        stride = db->header.stride;
    }
    x1 = area->x1;
    x2 = area->x2;
    y1 = area->y1;
    y2 = area->y2;
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 >= (int32_t)g_fb_xres) {
        x2 = (int32_t)g_fb_xres - 1;
    }
    if (y2 >= (int32_t)g_fb_yres) {
        y2 = (int32_t)g_fb_yres - 1;
    }
    if (x2 < x1 || y2 < y1) {
        lv_display_flush_ready(disp);
        return;
    }
    for (y = y1; y <= y2; y++) {
        size_t row_bytes = (size_t)(x2 - x1 + 1) * bpp;
        size_t dst_off = zk_fb_pixel_offset(&g_fb_geom, (unsigned)x1, (unsigned)y);
        size_t src_off = (size_t)y * stride + (size_t)x1 * bpp;
        if (dst_off + row_bytes > g_fb_map_len) {
            break;
        }
        memcpy(g_fb_map + dst_off, px + src_off, row_bytes);
    }
    lv_display_flush_ready(disp);
}

static void virt_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = g_pt;
    data->state = g_ptr_state;
    gate_pointer(data, &g_virt_down);
}

static void evdev_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (g_evdev_orig) {
        g_evdev_orig(indev, data);
    }
    gate_pointer(data, &g_evdev_down);
}

static int bit_is_set(int bit, const unsigned long *bits)
{
    unsigned long word = bits[bit / (int)(8 * sizeof(unsigned long))];
    return (int)((word >> (bit % (int)(8 * sizeof(unsigned long)))) & 1UL);
}

static int fd_has_mt(int fd)
{
    unsigned long bits[(ABS_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))];

    memset(bits, 0, sizeof bits);
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof bits), bits) < 0) {
        return 0;
    }
    return bit_is_set(ABS_MT_POSITION_X, bits) && bit_is_set(ABS_MT_POSITION_Y, bits);
}

static int handlers_event_path(const char *handlers, char *out, size_t out_sz)
{
    const char *p = handlers;

    while ((p = strstr(p, "event")) != NULL) {
        const char *end = p + 5;
        char num[16];
        size_t n = 0;
        if (p != handlers) {
            char prev = p[-1];
            int alnum = (prev >= '0' && prev <= '9') || (prev >= 'A' && prev <= 'Z') ||
                        (prev >= 'a' && prev <= 'z');
            if (alnum) {
                p += 5;
                continue;
            }
        }
        while (*end >= '0' && *end <= '9' && n + 1 < sizeof num) {
            num[n++] = *end++;
        }
        num[n] = '\0';
        if (n > 0) {
            int wrote = snprintf(out, out_sz, "/dev/input/event%s", num);
            if (wrote > 0 && (size_t)wrote < out_sz) {
                return 0;
            }
            return -1;
        }
        p += 5;
    }
    return -1;
}

static int name_has_rpi_ts(const char *line)
{
    const char *q = strchr(line, '"');
    if (!q) {
        return 0;
    }
    return strstr(q, "raspberrypi-ts") != NULL;
}

/* Bounded copy. snprintf %s trips -Wformat-truncation on the arm cross gcc. */
static void copy_path(char *dst, size_t dst_sz, const char *src)
{
    size_t i = 0;

    if (dst_sz == 0) {
        return;
    }
    while (src[i] && i + 1 < dst_sz) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Prefer the raspberrypi-ts event node. Otherwise the first device that
 * reports ABS_MT_POSITION_X and ABS_MT_POSITION_Y. */
static int autodetect_touch(char *out, size_t out_sz)
{
    FILE *f;
    char line[512];
    char preferred[64];
    char cands[64][64];
    int ncand = 0;
    int is_ts = 0;
    char path[64];
    int have_path = 0;
    int i;

    preferred[0] = '\0';
    path[0] = '\0';
    f = fopen("/proc/bus/input/devices", "r");
    if (!f) {
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '\n' || line[0] == '\0') {
            if (is_ts && have_path && !preferred[0]) {
                copy_path(preferred, sizeof preferred, path);
            } else if (have_path && ncand < 64) {
                copy_path(cands[ncand], sizeof cands[0], path);
                ncand++;
            }
            is_ts = 0;
            have_path = 0;
            path[0] = '\0';
            continue;
        }
        if (line[0] == 'N' && name_has_rpi_ts(line)) {
            is_ts = 1;
        } else if (line[0] == 'H' && handlers_event_path(line, path, sizeof path) == 0) {
            have_path = 1;
        }
    }
    if (is_ts && have_path && !preferred[0]) {
        copy_path(preferred, sizeof preferred, path);
    } else if (have_path && ncand < 64) {
        copy_path(cands[ncand], sizeof cands[0], path);
        ncand++;
    }
    fclose(f);

    if (preferred[0]) {
        int fd = open(preferred, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd >= 0) {
            close(fd);
            copy_path(out, out_sz, preferred);
            return 0;
        }
    }
    for (i = 0; i < ncand; i++) {
        int fd = open(cands[i], O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        if (fd_has_mt(fd)) {
            close(fd);
            copy_path(out, out_sz, cands[i]);
            return 0;
        }
        close(fd);
    }
    return -1;
}

static void read_axis(int fd, int code, int *mn, int *mx, int *ok)
{
    struct input_absinfo info;

    if (ioctl(fd, EVIOCGABS(code), &info) != 0) {
        return;
    }
    if (info.maximum == info.minimum) {
        return;
    }
    *mn = info.minimum;
    *mx = info.maximum;
    *ok = 1;
}

static void open_touch(const char *path, int swap, int flip_x, int flip_y)
{
    lv_indev_t *indev;
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    int have_x = 0;
    int have_y = 0;

    g_tfd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (g_tfd < 0) {
        fprintf(stderr, "touch: %s: %s\n", path, strerror(errno));
        return;
    }
    read_axis(g_tfd, ABS_MT_POSITION_X, &min_x, &max_x, &have_x);
    if (!have_x) {
        read_axis(g_tfd, ABS_X, &min_x, &max_x, &have_x);
    }
    read_axis(g_tfd, ABS_MT_POSITION_Y, &min_y, &max_y, &have_y);
    if (!have_y) {
        read_axis(g_tfd, ABS_Y, &min_y, &max_y, &have_y);
    }

    indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, path);
    if (!indev) {
        fprintf(stderr, "touch: cannot open %s\n", path);
        close(g_tfd);
        g_tfd = -1;
        return;
    }
    if (swap) {
        lv_evdev_set_swap_axes(indev, true);
    }
    if (flip_x && have_x) {
        int tmp = min_x;
        min_x = max_x;
        max_x = tmp;
    }
    if (flip_y && have_y) {
        int tmp = min_y;
        min_y = max_y;
        max_y = tmp;
    }
    if ((flip_x || flip_y || have_x || have_y) && have_x && have_y) {
        lv_evdev_set_calibration(indev, min_x, min_y, max_x, max_y);
    }
    g_evdev_orig = lv_indev_get_read_cb(indev);
    g_evdev_down = 0;
    if (g_evdev_orig) {
        lv_indev_set_read_cb(indev, evdev_read);
    }
}

static int open_memory(void)
{
    g_disp = lv_display_create(SNAP_W, SNAP_H);
    if (!g_disp) {
        return -1;
    }
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_XRGB8888);
    memset(g_frame, 0, sizeof g_frame);
    lv_display_set_buffers(g_disp, g_frame, NULL, sizeof g_frame, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(g_disp, mem_flush);
    return 0;
}

static int open_fb(const char *path)
{
    int fd;
    size_t n;
    zk_fb_geom_t geom;
    zk_fb_fmt_t fmt;
    char err[512];
    lv_color_format_t cf;
    uint32_t stride;
    size_t buf_bytes;
    const char *fmt_name;
    void *map;
    int real;

    g_fb_real = 0;
    g_fb_path[0] = '\0';
    /* Only the path the caller passed is opened. There is no default device. */
    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "fb: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (zk_fb_probe(fd, path, &geom, err, sizeof err) != 0) {
        fprintf(stderr, "fb: %s: %s\n", path, err);
        close(fd);
        return -1;
    }
    fmt = zk_fb_pick_format(&geom, err, sizeof err);
    if (fmt == ZK_FB_FMT_NONE) {
        fprintf(stderr, "fb: unsupported framebuffer format: %s\n", err);
        close(fd);
        return ZK_EXIT_FB_UNSUPPORTED;
    }
    if (geom.xres > 2147483647u || geom.yres > 2147483647u || geom.smem_len == 0) {
        fprintf(stderr, "fb: %s: frame is too large\n", path);
        close(fd);
        return -1;
    }
    map = mmap(NULL, geom.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "fb: %s: mmap: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    g_fb_fd = fd;
    g_fb_map = map;
    g_fb_map_len = geom.smem_len;
    g_fb_xres = geom.xres;
    g_fb_yres = geom.yres;
    g_fb_geom = geom;
    g_fb_px_bytes = fmt == ZK_FB_FMT_RGB565 ? 2u : 4u;
    /* Best-effort, and only on a real fbdev device. A missing blank ioctl is not fatal. */
    real = zk_fb_path_is_fbdev(path);
    if (real) {
        (void)ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
    }
    g_disp = lv_display_create((int32_t)geom.xres, (int32_t)geom.yres);
    if (!g_disp) {
        fprintf(stderr, "fb: create failed\n");
        fb_release();
        return -1;
    }
    cf = fmt == ZK_FB_FMT_RGB565 ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_XRGB8888;
    lv_display_set_color_format(g_disp, cf);
    lv_display_set_flush_cb(g_disp, fb_flush);
    stride = lv_draw_buf_width_to_stride(geom.xres, cf);
    if (stride == 0 || geom.yres > UINT32_MAX / stride) {
        fprintf(stderr, "fb: %s: frame is too large\n", path);
        lv_display_delete(g_disp);
        g_disp = NULL;
        fb_release();
        return -1;
    }
    g_fb_src_stride = stride;
    buf_bytes = (size_t)stride * (size_t)geom.yres;
    if (buf_bytes > UINT32_MAX) {
        fprintf(stderr, "fb: %s: frame is too large\n", path);
        lv_display_delete(g_disp);
        g_disp = NULL;
        fb_release();
        return -1;
    }
    g_fb_draw = malloc(buf_bytes);
    if (!g_fb_draw) {
        fprintf(stderr, "fb: %s: out of memory\n", path);
        lv_display_delete(g_disp);
        g_disp = NULL;
        fb_release();
        return -1;
    }
    memset(g_fb_draw, 0, buf_bytes);
    lv_display_set_buffers(g_disp, g_fb_draw, NULL, (uint32_t)buf_bytes, LV_DISPLAY_RENDER_MODE_FULL);
    n = strlen(path);
    if (n > 0 && n < sizeof g_fb_path) {
        memcpy(g_fb_path, path, n + 1);
        g_fb_real = real;
    }
    fmt_name = fmt == ZK_FB_FMT_RGB565 ? "RGB565" : "XRGB8888";
    if (geom.xoffset != 0 || geom.yoffset != 0) {
        fprintf(stderr, "fb: %s %ux%u %ubpp %s stride %u xoffset %u yoffset %u\n", path, geom.xres,
                geom.yres, geom.bpp, fmt_name, geom.line_length, geom.xoffset, geom.yoffset);
    } else {
        fprintf(stderr, "fb: %s %ux%u %ubpp %s stride %u\n", path, geom.xres, geom.yres, geom.bpp,
                fmt_name, geom.line_length);
    }
    return 0;
}

static int make_wake_pipe(void)
{
    int fds[2];

    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        fprintf(stderr, "wake pipe: %s\n", strerror(errno));
        return -1;
    }
    g_wake_r = fds[0];
    g_wake_w = fds[1];
    return 0;
}

int zk_platform_init(const zk_platform_opts_t *opts)
{
    char detected[64];
    const char *touch = NULL;
    int fb_rc;

    if (!opts) {
        return -1;
    }
    g_fb_real = 0;
    g_fb_path[0] = '\0';
    g_virt_down = 0;
    g_evdev_down = 0;
    lv_init();
    if (make_wake_pipe() != 0) {
        return -1;
    }
    if (opts->fb_path) {
        fb_rc = open_fb(opts->fb_path);
        if (fb_rc != 0) {
            return fb_rc;
        }
    } else if (open_memory() != 0) {
        fprintf(stderr, "display: create failed\n");
        return -1;
    }
    lv_tick_set_cb(tick_cb);
    lv_display_add_event_cb(g_disp, on_flush, LV_EVENT_FLUSH_FINISH, NULL);

    if (opts->virtual_pointer) {
        g_virt = lv_indev_create();
        if (!g_virt) {
            fprintf(stderr, "pointer: create failed\n");
            return -1;
        }
        lv_indev_set_type(g_virt, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(g_virt, virt_read);
        lv_indev_set_display(g_virt, g_disp);
    }

    if (opts->touch_path && opts->touch_path[0]) {
        touch = opts->touch_path;
    } else if (opts->autodetect_touch) {
        if (autodetect_touch(detected, sizeof detected) == 0) {
            touch = detected;
        } else {
            fprintf(stderr, "touch: no device found; continuing without touch\n");
        }
    }
    if (touch) {
        open_touch(touch, opts->touch_swap, opts->touch_flip_x, opts->touch_flip_y);
    }
    return 0;
}

lv_display_t *zk_platform_display(void)
{
    return g_disp;
}

uint32_t zk_platform_handle_timers(void)
{
    uint64_t before = g_frames;
    double t0 = zk_platform_mono();
    uint32_t wait = lv_timer_handler();
    double dt_ms = (zk_platform_mono() - t0) * 1000.0;

    if (g_frames != before) {
        sample_add(&g_render, &g_render_n, &g_render_cap, dt_ms);
    }
    return wait;
}

int zk_platform_busy(void)
{
    lv_indev_t *indev = NULL;

    if (g_ptr_state == LV_INDEV_STATE_PRESSED) {
        return 1;
    }
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
            return 1;
        }
    }
    if (lv_anim_count_running() > 0) {
        return 1;
    }
    return 0;
}

void zk_platform_wait(uint32_t timeout_ms)
{
    struct pollfd pf[2];
    int n = 0;
    char junk[64];

    if (g_tfd >= 0) {
        pf[n].fd = g_tfd;
        pf[n].events = POLLIN;
        pf[n].revents = 0;
        n++;
    }
    if (g_wake_r >= 0) {
        pf[n].fd = g_wake_r;
        pf[n].events = POLLIN;
        pf[n].revents = 0;
        n++;
    }
    for (;;) {
        int rc = poll(n ? pf : NULL, (nfds_t)n, (int)timeout_ms);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    if (g_wake_r >= 0) {
        while (read(g_wake_r, junk, sizeof junk) > 0) {
        }
    }
    drain_touch_fd();
}

void zk_platform_wake(void)
{
    char b = 1;
    if (g_wake_w >= 0) {
        ssize_t n = write(g_wake_w, &b, 1);
        (void)n;
    }
}

void zk_platform_pointer(int x, int y, int pressed)
{
    g_pt.x = x;
    g_pt.y = y;
    g_ptr_state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void zk_platform_note_injected_touch(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    queue_touch((int64_t)ts.tv_sec, (int64_t)(ts.tv_nsec / 1000));
}

const uint8_t *zk_platform_frame(int *w, int *h, int *stride)
{
    if (!g_have_frame) {
        return NULL;
    }
    if (w) {
        *w = SNAP_W;
    }
    if (h) {
        *h = SNAP_H;
    }
    if (stride) {
        *stride = SNAP_W * 4;
    }
    return g_snap;
}

int zk_snapshot_png(const char *file)
{
    lv_obj_t *scr;

    if (!g_disp || !file || !file[0]) {
        return -1;
    }
    scr = lv_screen_active();
    if (scr) {
        lv_obj_invalidate(scr);
    }
    lv_refr_now(g_disp);
    if (!g_have_frame) {
        fprintf(stderr, "shot: %s: no frame\n", file);
        return -1;
    }
    if (zk_png_write_xrgb8888(file, g_snap, SNAP_W, SNAP_H, SNAP_W * 4) != 0) {
        fprintf(stderr, "shot: %s: write failed\n", file);
        return -1;
    }
    return 0;
}

static void read_mem(long *rss_kb, long *hwm_kb)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];

    *rss_kb = 0;
    *hwm_kb = 0;
    if (!f) {
        return;
    }
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            *rss_kb = atol(line + 6);
        } else if (strncmp(line, "VmHWM:", 6) == 0) {
            *hwm_kb = atol(line + 6);
        }
    }
    fclose(f);
}

void zk_platform_print_stats(int enabled)
{
    unsigned long ut = 0;
    unsigned long st = 0;
    unsigned long start = 0;
    long hz;
    long rss = 0;
    long hwm = 0;
    double user_s;
    double sys_s;
    double elapsed;
    double cpu;
    double r_avg = 0;
    double r_p95 = 0;
    double t_avg = 0;
    double t_p95 = 0;
    double t_max = 0;

    if (!enabled) {
        return;
    }
    hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) {
        hz = 100;
    }
    read_proc_stat(&ut, &st, &start);
    read_mem(&rss, &hwm);
    user_s = (double)ut / (double)hz;
    sys_s = (double)st / (double)hz;
    elapsed = zk_platform_mono() - g_proc_mono;
    if (elapsed < 1e-6) {
        elapsed = 1e-6;
    }
    cpu = (user_s + sys_s) / elapsed * 100.0;
    summarize(g_render, g_render_n, &r_avg, &r_p95, NULL);
    summarize(g_lat, g_lat_n, &t_avg, &t_p95, &t_max);
    fprintf(stderr,
            "{\"first_frame_ms\":%.3f,\"first_frame_main_ms\":%.3f,\"frames\":%llu,"
            "\"render_ms_avg\":%.3f,\"render_ms_p95\":%.3f,"
            "\"touch_events\":%llu,"
            "\"touch_to_frame_avg_ms\":%.3f,\"touch_to_frame_p95_ms\":%.3f,\"touch_to_frame_max_ms\":%.3f,"
            "\"vm_rss_kb\":%ld,\"vm_hwm_kb\":%ld,"
            "\"cpu_user_s\":%.4f,\"cpu_sys_s\":%.4f,\"cpu_pct\":%.2f,\"power_transitions\":%d}\n",
            g_got_first ? g_first_ms : 0.0, g_got_first ? g_first_main_ms : 0.0,
            (unsigned long long)g_frames, r_avg, r_p95, (unsigned long long)g_touch_events, t_avg, t_p95,
            t_max, rss, hwm, user_s, sys_s, cpu, g_power_transitions);
    fflush(stderr);
}

void zk_platform_bind_power(zk_power_t *power)
{
    g_power = power;
    g_virt_down = 0;
    g_evdev_down = 0;
}

int zk_platform_fb_is_real(void)
{
    return g_fb_real;
}

int zk_platform_fbio_blank(void *ctx, int powerdown)
{
    int fd;
    int arg;
    int rc;

    (void)ctx;
    if (!g_fb_real || !g_fb_path[0]) {
        return -1;
    }
    fd = open(g_fb_path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    arg = powerdown ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK;
    rc = ioctl(fd, FBIOBLANK, arg);
    close(fd);
    return rc == 0 ? 0 : -1;
}

void zk_platform_set_power_transitions(int n)
{
    g_power_transitions = n;
}
