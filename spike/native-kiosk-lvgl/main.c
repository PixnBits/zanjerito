/* 800x480 LVGL v9.2 home screen. Read-only: GET /api/status only, never a write. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "lvgl.h"

enum {
    SCR_W = 800,
    SCR_H = 480,
    HEADER_H = 52,
    BODY_Y = 60,
    RIGHT_X = 540,
    RIGHT_W = 252,
    STOP_H = 190,
    PAUSE_H = 100,
    TILE_W = 258,
    TILE_H = 204,
    MENU_S = 80,
    MAX_ST = 4,
    MAX_ON = 8,
    TOUCH_Q = 256
};

typedef struct {
    char id[16];
    char title[48];
    char color[16];
} station_t;

typedef struct {
    char phase[32];
    char title[32];
    char pause_label[64];
    char next_run[64];
    char on_ids[MAX_ON][16];
    int on_count;
    int paused;
    int show_rain;
    double rain;
    station_t stations[MAX_ST];
    int station_count;
} app_state_t;

typedef struct {
    int64_t sec;
    int64_t usec;
} touch_ts_t;

typedef struct {
    lv_obj_t *tile;
    lv_obj_t *title;
    lv_obj_t *state;
    lv_obj_t *bar;
    int pulsing;
    char id[16];
} tile_ui_t;

static app_state_t g;
static tile_ui_t g_tiles[MAX_ST];
static int g_tile_n;

static lv_obj_t *g_title_lbl;
static lv_obj_t *g_clock_lbl;
static lv_obj_t *g_rain;
static lv_obj_t *g_rain_lbl;
static lv_obj_t *g_pause_lbl;
static lv_obj_t *g_next_lbl;
static lv_obj_t *g_menu_panel;
static char g_clock_txt[8];

static const char *g_fb;
static const char *g_touch;
static const char *g_fixture;
static const char *g_shot;
static char g_api_host[64];
static int g_api_port;
static int g_have_api;
static int g_anim;
static int g_duration = -1;
static int g_stats;
static int g_shot_status; /* 0 none yet, 1 written, -1 failed */

static int g_tfd = -1;
static lv_indev_read_cb_t g_orig_read;
static touch_ts_t g_tq[TOUCH_Q];
static int g_tq_n;
static int g_touch_count;
static double g_touch_sum_ms;
static double g_touch_max_ms;

static uint64_t g_frames;
static int g_got_first;
static double g_first_frame_ms = -1;
static double g_proc_start_mono;
static double g_main_mono;

static volatile sig_atomic_t g_stop;

static uint8_t g_frame[SCR_W * SCR_H * 4] __attribute__((aligned(64)));

static void refresh_ui(void);
static void build_ui(void);

/* Bounded copy. Avoids snprintf truncation warnings on short fields. */
static void copy_str(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    if (n == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < n) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static double mono_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static uint32_t tick_cb(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int read_proc_stat(unsigned long *utime, unsigned long *stime, unsigned long *starttime)
{
    FILE *f = fopen("/proc/self/stat", "r");
    char buf[1024];
    char *rp;
    int n;
    if (!f) return -1;
    if (!fgets(buf, sizeof buf, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    rp = strrchr(buf, ')');
    if (!rp || rp[1] == '\0') return -1;
    /* fields 3..22; utime=14 stime=15 starttime=22 */
    n = sscanf(rp + 2,
               "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %lu",
               utime, stime, starttime);
    return n == 3 ? 0 : -1;
}

static void mark_process_start(void)
{
    struct timespec mono;
    double up = 0;
    unsigned long ut = 0, st = 0, start = 0;
    long hz;
    FILE *f;
    clock_gettime(CLOCK_MONOTONIC, &mono);
    g_main_mono = (double)mono.tv_sec + (double)mono.tv_nsec / 1e9;
    f = fopen("/proc/uptime", "r");
    if (f) {
        if (fscanf(f, "%lf", &up) != 1) up = 0;
        fclose(f);
    }
    hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) hz = 100;
    if (up > 0 && read_proc_stat(&ut, &st, &start) == 0) {
        double age = up - (double)start / (double)hz;
        if (age < 0) age = 0;
        /* Age before main (from starttime) plus the monotonic clock taken here. */
        g_proc_start_mono = g_main_mono - age;
    } else {
        g_proc_start_mono = g_main_mono;
    }
}

static void read_mem(long *rss_kb, long *hwm_kb)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    *rss_kb = 0;
    *hwm_kb = 0;
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) *rss_kb = atol(line + 6);
        else if (strncmp(line, "VmHWM:", 6) == 0) *hwm_kb = atol(line + 6);
    }
    fclose(f);
}

static void print_stats(void)
{
    unsigned long ut = 0, st = 0, start = 0;
    long hz, rss = 0, hwm = 0;
    double user_s, sys_s, elapsed, cpu;
    double avg;
    if (!g_stats) return;
    hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) hz = 100;
    read_proc_stat(&ut, &st, &start);
    read_mem(&rss, &hwm);
    user_s = (double)ut / (double)hz;
    sys_s = (double)st / (double)hz;
    elapsed = mono_now() - g_proc_start_mono;
    if (elapsed < 1e-6) elapsed = 1e-6;
    cpu = (user_s + sys_s) / elapsed * 100.0;
    avg = g_touch_count ? g_touch_sum_ms / (double)g_touch_count : 0;
    printf("{\"first_frame_ms\":%.3f,\"frames\":%llu,\"vm_rss_kb\":%ld,\"vm_hwm_kb\":%ld,"
           "\"cpu_user_s\":%.4f,\"cpu_sys_s\":%.4f,\"cpu_pct\":%.2f,"
           "\"touch_count\":%d,\"touch_avg_ms\":%.3f,\"touch_max_ms\":%.3f}\n",
           g_first_frame_ms, (unsigned long long)g_frames, rss, hwm,
           user_s, sys_s, cpu, g_touch_count, avg, g_touch_max_ms);
}

static uint32_t parse_color(const char *name)
{
    if (!name || !name[0]) return 0x546E7A;
    if (name[0] == '#') return (uint32_t)strtoul(name + 1, NULL, 16);
    if (strcmp(name, "red") == 0) return 0xC62828;
    if (strcmp(name, "blue") == 0) return 0x1565C0;
    if (strcmp(name, "green") == 0) return 0x2E7D32;
    if (strcmp(name, "orange") == 0) return 0xEF6C00;
    if (strcmp(name, "yellow") == 0) return 0xF9A825;
    if (strcmp(name, "teal") == 0) return 0x00838F;
    if (strcmp(name, "purple") == 0) return 0x6A1B9A;
    if (strcmp(name, "gray") == 0 || strcmp(name, "grey") == 0) return 0x546E7A;
    return 0x546E7A;
}

static uint32_t darken(uint32_t c)
{
    uint32_t r = ((c >> 16) & 255u) * 55u / 100u;
    uint32_t gch = ((c >> 8) & 255u) * 55u / 100u;
    uint32_t b = (c & 255u) * 55u / 100u;
    return (r << 16) | (gch << 8) | b;
}

static void add_on(const char *val)
{
    char buf[128];
    char *p;
    copy_str(buf, sizeof buf, val);
    p = buf;
    while (*p && g.on_count < MAX_ON) {
        char *comma = strchr(p, ',');
        char *end;
        if (comma) *comma = '\0';
        while (*p == ' ') p++;
        end = p + strlen(p);
        while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
            *end = '\0';
        }
        if (*p) {
            copy_str(g.on_ids[g.on_count], sizeof g.on_ids[0], p);
            g.on_count++;
        }
        if (!comma) break;
        p = comma + 1;
    }
}

static void load_defaults(void)
{
    static const char *colors[] = {"red", "blue", "green", "orange"};
    int i;
    memset(&g, 0, sizeof g);
    copy_str(g.phase, sizeof g.phase, "Idle");
    copy_str(g.title, sizeof g.title, "Home");
    copy_str(g.next_run, sizeof g.next_run, "Tomorrow 08:23");
    g.rain = 0.24;
    g.show_rain = 1;
    for (i = 0; i < 4; i++) {
        snprintf(g.stations[i].id, sizeof g.stations[i].id, "az%02d", i + 1);
        snprintf(g.stations[i].title, sizeof g.stations[i].title, "Test Station %d", i + 1);
        copy_str(g.stations[i].color, sizeof g.stations[i].color, colors[i]);
    }
    g.station_count = 4;
}

static int load_fixture(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    if (!f) {
        fprintf(stderr, "fixture: %s: %s\n", path, strerror(errno));
        return -1;
    }
    memset(&g, 0, sizeof g);
    copy_str(g.title, sizeof g.title, "Home");
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        char *eq;
        char *nl;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        nl = strpbrk(p, "\r\n");
        if (nl) *nl = '\0';
        eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        eq++;
        if (strcmp(p, "phase") == 0) copy_str(g.phase, sizeof g.phase, eq);
        else if (strcmp(p, "on") == 0) add_on(eq);
        else if (strcmp(p, "paused") == 0) g.paused = atoi(eq) ? 1 : 0;
        else if (strcmp(p, "rain") == 0) {
            g.rain = atof(eq);
            g.show_rain = 1;
        } else if (strcmp(p, "show") == 0) g.show_rain = atoi(eq) ? 1 : 0;
        else if (strcmp(p, "next") == 0) copy_str(g.next_run, sizeof g.next_run, eq);
        else if (strcmp(p, "pause_label") == 0) copy_str(g.pause_label, sizeof g.pause_label, eq);
        else if (strcmp(p, "title") == 0) copy_str(g.title, sizeof g.title, eq);
        else if (strcmp(p, "st") == 0 && g.station_count < MAX_ST) {
            char *id = eq;
            char *title = strchr(id, '|');
            char *color;
            station_t *st;
            if (!title) continue;
            *title = '\0';
            title++;
            color = strchr(title, '|');
            if (!color) continue;
            *color = '\0';
            color++;
            st = &g.stations[g.station_count];
            copy_str(st->id, sizeof st->id, id);
            copy_str(st->title, sizeof st->title, title);
            copy_str(st->color, sizeof st->color, color);
            g.station_count++;
        }
    }
    fclose(f);
    return 0;
}

static const char *after_key(const char *json, const char *key)
{
    const char *p = json;
    size_t klen = strlen(key);
    while ((p = strstr(p, key)) != NULL) {
        const char *q = p + klen;
        if (*q == '"') {
            q++;
            q = strchr(q, ':');
            if (!q) return NULL;
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
            return q;
        }
        p += klen;
    }
    return NULL;
}

static int take_string(const char *json, const char *key, char *out, size_t out_sz)
{
    const char *p = after_key(json, key);
    size_t i = 0;
    if (!p || *p != '"') return -1;
    p++;
    while (*p && *p != '"' && i + 1 < out_sz) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

static int take_bool(const char *json, const char *key, int *out)
{
    const char *p = after_key(json, key);
    if (!p) return -1;
    if (strncmp(p, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int parse_id_array(const char *p, char ids[][16], int cap)
{
    int n = 0;
    if (*p == 'n') return 0; /* null */
    if (*p != '[') return -1;
    while (*p && *p != ']' && n < cap) {
        const char *q1 = strchr(p, '"');
        const char *q2;
        int len;
        if (!q1) break;
        q2 = strchr(q1 + 1, '"');
        if (!q2) break;
        if (strchr(p, ']') && strchr(p, ']') < q1) break;
        len = (int)(q2 - (q1 + 1));
        if (len > 15) len = 15;
        if (len < 0) len = 0;
        memcpy(ids[n], q1 + 1, (size_t)len);
        ids[n][len] = '\0';
        n++;
        p = q2 + 1;
    }
    return n;
}

static int same_ids(char a[][16], int na, char b[][16], int nb)
{
    int i;
    if (na != nb) return 0;
    for (i = 0; i < na; i++) {
        if (strcmp(a[i], b[i]) != 0) return 0;
    }
    return 1;
}

/* Pull the fields the spike cares about. Returns 1 if the screen should update. */
static int apply_json(const char *json)
{
    int changed = 0;
    char phase[32];
    char label[64];
    int paused = 0;
    int show = g.show_rain;
    const char *rs;
    const char *p;

    if (take_string(json, "\"phase\"", phase, sizeof phase) == 0) {
        if (strcmp(g.phase, phase) != 0) {
            copy_str(g.phase, sizeof g.phase, phase);
            changed = 1;
        }
    }
    if (take_bool(json, "\"paused\"", &paused) == 0 && paused != g.paused) {
        g.paused = paused;
        changed = 1;
    }
    if (take_string(json, "\"paused_label\"", label, sizeof label) == 0) {
        if (strcmp(g.pause_label, label) != 0) {
            copy_str(g.pause_label, sizeof g.pause_label, label);
            changed = 1;
        }
    }
    p = after_key(json, "\"stations_on\"");
    if (p) {
        char ids[MAX_ON][16];
        int n = parse_id_array(p, ids, MAX_ON);
        if (n >= 0 && !same_ids(g.on_ids, g.on_count, ids, n)) {
            int i;
            g.on_count = n;
            for (i = 0; i < n; i++) copy_str(g.on_ids[i], sizeof g.on_ids[i], ids[i]);
            changed = 1;
        }
    }
    rs = strstr(json, "\"rain_strip\"");
    if (rs) {
        const char *end = rs;
        const char *inches;
        const char *showp;
        int depth = 0;
        int seen = 0;
        for (; *end; end++) {
            if (*end == '{') {
                depth++;
                seen = 1;
            } else if (*end == '}') {
                depth--;
                if (seen && depth == 0) break;
            }
        }
        inches = strstr(rs, "\"inches\"");
        if (inches && inches < end) {
            inches = strchr(inches, ':');
            if (inches) {
                double v = strtod(inches + 1, NULL);
                if (v != g.rain) {
                    g.rain = v;
                    changed = 1;
                }
            }
        }
        showp = strstr(rs, "\"show\"");
        if (showp && showp < end) {
            showp = strchr(showp, ':');
            if (showp) {
                showp++;
                while (*showp == ' ') showp++;
                if (strncmp(showp, "true", 4) == 0) show = 1;
                else if (strncmp(showp, "false", 5) == 0) show = 0;
                if (show != g.show_rain) {
                    g.show_rain = show;
                    changed = 1;
                }
            }
        }
    }
    return changed;
}

/* GET /api/status only. This client never sends a request body or any other method. */
static int api_get_status(char *body, size_t body_sz)
{
    int fd;
    struct sockaddr_in addr;
    struct timeval tv;
    char req[192];
    int nreq;
    size_t off = 0;
    size_t got = 0;

    if (inet_pton(AF_INET, g_api_host, &addr.sin_addr) != 1) {
        fprintf(stderr, "api: host must be an IPv4 address\n");
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)g_api_port);
    if (inet_pton(AF_INET, g_api_host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return -1;
    }
    nreq = snprintf(req, sizeof req,
                    "GET /api/status HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
                    g_api_host, g_api_port);
    if (nreq < 0 || (size_t)nreq >= sizeof req) {
        close(fd);
        return -1;
    }
    while (off < (size_t)nreq) {
        ssize_t w = send(fd, req + off, (size_t)nreq - off, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        off += (size_t)w;
    }
    while (got + 1 < body_sz) {
        ssize_t r = recv(fd, body + got, body_sz - 1 - got, 0);
        if (r == 0) break;
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        got += (size_t)r;
    }
    body[got] = '\0';
    close(fd);
    return got > 0 ? 0 : -1;
}

static void poll_api(void)
{
    char buf[8192];
    char *json;
    if (!g_have_api) return;
    if (api_get_status(buf, sizeof buf) != 0) {
        fprintf(stderr, "api: GET /api/status failed\n");
        return;
    }
    json = strstr(buf, "\r\n\r\n");
    if (json) json += 4;
    else json = buf;
    if (apply_json(json)) refresh_ui();
}

static int station_on(const char *id)
{
    int i;
    for (i = 0; i < g.on_count; i++) {
        if (strcmp(g.on_ids[i], id) == 0) return 1;
    }
    return 0;
}

static void plain(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void style_pressed(lv_obj_t *obj, uint32_t base)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(base), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(darken(base)), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 5, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void on_readonly_click(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    fprintf(stderr, "readonly: would POST /api/run/cancel\n");
}

static void on_menu_click(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_obj_has_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN))
        lv_obj_remove_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
}

static void on_panel_click(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_add_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
}

static void pulse_exec(void *bar, int32_t v)
{
    lv_bar_set_value(bar, v, LV_ANIM_OFF);
}

static void start_pulse(lv_obj_t *bar)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, pulse_exec);
    lv_anim_set_values(&a, 12, 100);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_playback_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_early_apply(&a, false);
    lv_anim_start(&a);
}

static void refresh_ui(void)
{
    char buf[96];
    int i;
    if (!g_title_lbl) return;

    if (g.paused) {
        if (g.pause_label[0]) lv_label_set_text(g_title_lbl, g.pause_label);
        else lv_label_set_text(g_title_lbl, "Paused");
        lv_label_set_text(g_pause_lbl, "Resume");
    } else {
        lv_label_set_text(g_title_lbl, g.title[0] ? g.title : "Home");
        lv_label_set_text(g_pause_lbl, "Pause");
    }

    snprintf(buf, sizeof buf, "Rain %.2f in / 24h", g.rain);
    lv_label_set_text(g_rain_lbl, buf);
    if (g.show_rain) lv_obj_remove_flag(g_rain, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_rain, LV_OBJ_FLAG_HIDDEN);

    snprintf(buf, sizeof buf, "Next: %s", g.next_run);
    lv_label_set_text(g_next_lbl, buf);

    for (i = 0; i < g_tile_n; i++) {
        int on = station_on(g_tiles[i].id);
        lv_label_set_text(g_tiles[i].state, on ? "ON" : "idle");
        if (on && g_anim) {
            lv_obj_remove_flag(g_tiles[i].bar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(g_tiles[i].bar, 65, LV_ANIM_OFF);
            if (!g_tiles[i].pulsing) {
                start_pulse(g_tiles[i].bar);
                g_tiles[i].pulsing = 1;
            }
        } else {
            if (g_tiles[i].pulsing) {
                lv_anim_delete(g_tiles[i].bar, pulse_exec);
                g_tiles[i].pulsing = 0;
            }
            if (on) {
                lv_obj_remove_flag(g_tiles[i].bar, LV_OBJ_FLAG_HIDDEN);
                lv_bar_set_value(g_tiles[i].bar, 100, LV_ANIM_OFF);
            } else {
                lv_obj_add_flag(g_tiles[i].bar, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    return lbl;
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *header;
    lv_obj_t *stop;
    lv_obj_t *stop_lbl;
    lv_obj_t *pause;
    lv_obj_t *menu;
    lv_obj_t *menu_lbl;
    lv_obj_t *panel_lbl;
    int i;

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_20, 0);
    plain(scr);

    header = lv_obj_create(scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, SCR_W, HEADER_H);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x121A2B), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    plain(header);

    g_title_lbl = make_label(header, "Home", &lv_font_montserrat_28, 0xF4F7FB);
    lv_obj_align(g_title_lbl, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_width(g_title_lbl, 220);
    lv_label_set_long_mode(g_title_lbl, LV_LABEL_LONG_CLIP);

    g_clock_lbl = make_label(header, "--:--", &lv_font_montserrat_28, 0xF4F7FB);
    lv_obj_align(g_clock_lbl, LV_ALIGN_RIGHT_MID, -16, 0);

    g_rain = lv_obj_create(header);
    lv_obj_set_style_bg_color(g_rain, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_bg_opa(g_rain, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_rain, 16, 0);
    lv_obj_set_style_pad_hor(g_rain, 14, 0);
    lv_obj_set_style_pad_ver(g_rain, 4, 0);
    lv_obj_set_style_border_width(g_rain, 0, 0);
    lv_obj_set_size(g_rain, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    plain(g_rain);
    g_rain_lbl = make_label(g_rain, "Rain", &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_align(g_rain, LV_ALIGN_CENTER, 0, 0);

    for (i = 0; i < g.station_count && i < MAX_ST; i++) {
        int col = i % 2;
        int row = i / 2;
        uint32_t colr = parse_color(g.stations[i].color);
        lv_obj_t *tile = lv_button_create(scr);
        lv_obj_t *bar;
        g_tiles[i].tile = tile;
        copy_str(g_tiles[i].id, sizeof g_tiles[i].id, g.stations[i].id);
        lv_obj_set_pos(tile, 8 + col * (TILE_W + 8), BODY_Y + row * (TILE_H + 8));
        lv_obj_set_size(tile, TILE_W, TILE_H);
        style_pressed(tile, colr);
        lv_obj_add_event_cb(tile, on_readonly_click, LV_EVENT_CLICKED, NULL);

        g_tiles[i].title = make_label(tile, g.stations[i].title, &lv_font_montserrat_28, 0xFFFFFF);
        lv_obj_set_pos(g_tiles[i].title, 14, 18);
        lv_obj_set_width(g_tiles[i].title, TILE_W - 28);
        lv_label_set_long_mode(g_tiles[i].title, LV_LABEL_LONG_CLIP);

        g_tiles[i].state = make_label(tile, "idle", &lv_font_montserrat_28, 0xFFFFFF);
        lv_obj_set_pos(g_tiles[i].state, 14, 78);

        bar = lv_bar_create(tile);
        g_tiles[i].bar = bar;
        lv_obj_set_pos(bar, 14, TILE_H - 40);
        lv_obj_set_size(bar, TILE_W - 28, 18);
        lv_bar_set_range(bar, 0, 100);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x102027), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 8, LV_PART_INDICATOR);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        g_tile_n++;
    }

    stop = lv_button_create(scr);
    lv_obj_set_pos(stop, RIGHT_X, BODY_Y);
    lv_obj_set_size(stop, RIGHT_W, STOP_H);
    style_pressed(stop, 0xD32F2F);
    lv_obj_set_style_radius(stop, 16, LV_PART_MAIN);
    lv_obj_set_style_text_font(stop, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_add_event_cb(stop, on_readonly_click, LV_EVENT_CLICKED, NULL);
    stop_lbl = make_label(stop, "STOP", &lv_font_montserrat_40, 0xFFFFFF);
    lv_obj_align(stop_lbl, LV_ALIGN_CENTER, 0, 0);

    pause = lv_button_create(scr);
    lv_obj_set_pos(pause, RIGHT_X, BODY_Y + STOP_H + 6);
    lv_obj_set_size(pause, RIGHT_W, PAUSE_H);
    style_pressed(pause, 0x455A64);
    lv_obj_set_style_text_font(pause, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_add_event_cb(pause, on_readonly_click, LV_EVENT_CLICKED, NULL);
    g_pause_lbl = make_label(pause, "Pause", &lv_font_montserrat_40, 0xFFFFFF);
    lv_obj_align(g_pause_lbl, LV_ALIGN_CENTER, 0, 0);

    g_next_lbl = make_label(scr, "Next:", &lv_font_montserrat_20, 0xD0D7E2);
    lv_obj_set_pos(g_next_lbl, RIGHT_X, BODY_Y + STOP_H + 6 + PAUSE_H + 8);
    lv_obj_set_width(g_next_lbl, RIGHT_W);
    lv_label_set_long_mode(g_next_lbl, LV_LABEL_LONG_CLIP);

    menu = lv_button_create(scr);
    lv_obj_set_pos(menu, SCR_W - 8 - MENU_S, SCR_H - 8 - MENU_S);
    lv_obj_set_size(menu, MENU_S, MENU_S);
    style_pressed(menu, 0x37474F);
    lv_obj_add_event_cb(menu, on_menu_click, LV_EVENT_CLICKED, NULL);
    menu_lbl = make_label(menu, "...", &lv_font_montserrat_40, 0xFFFFFF);
    lv_obj_align(menu_lbl, LV_ALIGN_CENTER, 0, 0);

    g_menu_panel = lv_obj_create(scr);
    lv_obj_set_pos(g_menu_panel, 16, 150);
    lv_obj_set_size(g_menu_panel, 360, 120);
    lv_obj_set_style_bg_color(g_menu_panel, lv_color_hex(0x1B2636), 0);
    lv_obj_set_style_bg_opa(g_menu_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_menu_panel, 14, 0);
    lv_obj_set_style_border_width(g_menu_panel, 2, 0);
    lv_obj_set_style_border_color(g_menu_panel, lv_color_hex(0x90A4AE), 0);
    lv_obj_set_style_border_opa(g_menu_panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_menu_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_menu_panel, on_panel_click, LV_EVENT_CLICKED, NULL);
    panel_lbl = make_label(g_menu_panel, "Menu", &lv_font_montserrat_28, 0xFFFFFF);
    lv_obj_align(panel_lbl, LV_ALIGN_CENTER, 0, 0);

    refresh_ui();
}

static void update_clock(void)
{
    time_t now;
    struct tm tm;
    char buf[8];
    if (!g_clock_lbl) return;
    now = time(NULL);
    localtime_r(&now, &tm);
    if (tm.tm_hour < 0 || tm.tm_hour > 23 || tm.tm_min < 0 || tm.tm_min > 59) return;
    buf[0] = (char)('0' + tm.tm_hour / 10);
    buf[1] = (char)('0' + tm.tm_hour % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + tm.tm_min / 10);
    buf[4] = (char)('0' + tm.tm_min % 10);
    buf[5] = '\0';
    if (strcmp(buf, g_clock_txt) != 0) {
        memcpy(g_clock_txt, buf, 6);
        lv_label_set_text(g_clock_lbl, g_clock_txt);
    }
}

static int write_bgra(const char *path, const uint8_t *src, uint32_t stride)
{
    FILE *f;
    uint8_t row[SCR_W * 4];
    int y;
    if (!src || stride < (uint32_t)SCR_W * 4) return -1;
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "shot: %s: %s\n", path, strerror(errno));
        return -1;
    }
    for (y = 0; y < SCR_H; y++) {
        int x;
        memcpy(row, src + (size_t)y * stride, sizeof row);
        for (x = 0; x < SCR_W; x++) row[x * 4 + 3] = 0xFF;
        if (fwrite(row, 1, sizeof row, f) != sizeof row) {
            fclose(f);
            return -1;
        }
    }
    if (fclose(f) != 0) return -1;
    return 0;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (g_shot && g_shot_status == 0 && lv_display_flush_is_last(disp)) {
        uint32_t stride = (uint32_t)SCR_W * 4;
        const uint8_t *src = px_map;
        lv_draw_buf_t *db = lv_display_get_buf_active(disp);
        if (db && db->header.stride >= (uint32_t)SCR_W * 4) {
            stride = db->header.stride;
            if (area->x1 != 0 || area->y1 != 0) src = db->data;
        }
        g_shot_status = write_bgra(g_shot, src, stride) == 0 ? 1 : -1;
    }
    lv_display_flush_ready(disp);
}

static void on_flush_event(lv_event_t *e)
{
    lv_display_t *disp;
    if (lv_event_get_code(e) != LV_EVENT_FLUSH_FINISH) return;
    disp = lv_event_get_target(e);
    if (!lv_display_flush_is_last(disp)) return;
    if (!g_got_first) {
        g_got_first = 1;
        g_first_frame_ms = (mono_now() - g_proc_start_mono) * 1000.0;
        if (g_first_frame_ms < 0) g_first_frame_ms = 0;
    }
    g_frames++;
    if (g_tq_n > 0) {
        struct timespec now;
        int i;
        clock_gettime(CLOCK_REALTIME, &now);
        for (i = 0; i < g_tq_n; i++) {
            double ms = (double)(now.tv_sec - g_tq[i].sec) * 1000.0
                        + (double)now.tv_nsec / 1e6
                        - (double)g_tq[i].usec / 1000.0;
            if (ms < 0) ms = 0;
            g_touch_count++;
            g_touch_sum_ms += ms;
            if (ms > g_touch_max_ms) g_touch_max_ms = ms;
        }
        g_tq_n = 0;
    }
}

static void note_touch(const struct input_event *ev)
{
    int interesting = 0;
    if (ev->type == EV_KEY && (ev->code == BTN_TOUCH || ev->code == BTN_LEFT || ev->code == BTN_TOOL_FINGER))
        interesting = 1;
    if (ev->type == EV_ABS && (ev->code == ABS_X || ev->code == ABS_Y
                               || ev->code == ABS_MT_POSITION_X || ev->code == ABS_MT_POSITION_Y
                               || ev->code == ABS_MT_TRACKING_ID))
        interesting = 1;
    if (!interesting) return;
    if (g_tq_n >= TOUCH_Q) {
        memmove(&g_tq[0], &g_tq[1], sizeof(g_tq[0]) * (TOUCH_Q - 1));
        g_tq_n = TOUCH_Q - 1;
    }
    g_tq[g_tq_n].sec = (int64_t)ev->time.tv_sec;
    g_tq[g_tq_n].usec = (int64_t)ev->time.tv_usec;
    g_tq_n++;
}

static void drain_touch(void)
{
    struct input_event ev;
    if (g_tfd < 0) return;
    while (read(g_tfd, &ev, sizeof ev) == (ssize_t)sizeof ev) note_touch(&ev);
}

/* Record kernel timestamps, then let evdev consume its own read-only fd. No grab. */
static void touch_read_wrap(lv_indev_t *indev, lv_indev_data_t *data)
{
    drain_touch();
    if (g_orig_read) g_orig_read(indev, data);
}

static void open_touch(const char *path)
{
    lv_indev_t *indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, path);
    if (!indev) {
        fprintf(stderr, "touch: cannot open %s\n", path);
        return;
    }
    /* Second fd is passive: O_RDONLY, nonblocking, and never EVIOCGRAB. */
    g_tfd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (g_tfd < 0) {
        fprintf(stderr, "touch: timestamp fd: %s\n", strerror(errno));
        return;
    }
    g_orig_read = lv_indev_get_read_cb(indev);
    lv_indev_set_read_cb(indev, touch_read_wrap);
}

static lv_display_t *open_display(void)
{
    lv_display_t *disp;
    if (g_fb && !g_shot) {
        disp = lv_linux_fbdev_create();
        if (!disp) return NULL;
        lv_linux_fbdev_set_file(disp, g_fb);
    } else {
        if (g_fb && g_shot)
            fprintf(stderr, "shot: memory display; not opening framebuffer\n");
        disp = lv_display_create(SCR_W, SCR_H);
        if (!disp) return NULL;
        lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
        memset(g_frame, 0, sizeof g_frame);
        lv_display_set_buffers(disp, g_frame, NULL, sizeof g_frame, LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(disp, flush_cb);
    }
    lv_display_add_event_cb(disp, on_flush_event, LV_EVENT_FLUSH_FINISH, NULL);
    return disp;
}

static void sleep_ms(uint32_t ms)
{
    struct timespec ts;
    if (ms == 0) return;
    if (ms == LV_NO_TIMER_READY) ms = 33;
    if (ms > 33) ms = 33;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [-fb /dev/fb0] [-touch /dev/input/eventN] [-fixture PATH]\n"
            "          [-api HOST:PORT] [-anim] [-duration SEC] [-stats] [-shot FILE]\n",
            argv0);
}

int main(int argc, char **argv)
{
    int i;
    lv_display_t *disp;
    double next_api = 0;

    mark_process_start();
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-fb") == 0 && i + 1 < argc) g_fb = argv[++i];
        else if (strcmp(argv[i], "-touch") == 0 && i + 1 < argc) g_touch = argv[++i];
        else if (strcmp(argv[i], "-fixture") == 0 && i + 1 < argc) g_fixture = argv[++i];
        else if (strcmp(argv[i], "-api") == 0 && i + 1 < argc) {
            const char *spec = argv[++i];
            const char *colon = strrchr(spec, ':');
            size_t n;
            if (!colon || colon == spec) {
                usage(argv[0]);
                return 2;
            }
            n = (size_t)(colon - spec);
            if (n >= sizeof g_api_host) {
                usage(argv[0]);
                return 2;
            }
            memcpy(g_api_host, spec, n);
            g_api_host[n] = '\0';
            g_api_port = atoi(colon + 1);
            if (g_api_port <= 0 || g_api_port > 65535) {
                usage(argv[0]);
                return 2;
            }
            g_have_api = 1;
        } else if (strcmp(argv[i], "-anim") == 0) g_anim = 1;
        else if (strcmp(argv[i], "-duration") == 0 && i + 1 < argc) g_duration = atoi(argv[++i]);
        else if (strcmp(argv[i], "-stats") == 0) g_stats = 1;
        else if (strcmp(argv[i], "-shot") == 0 && i + 1 < argc) g_shot = argv[++i];
        else {
            usage(argv[0]);
            return 2;
        }
    }

    if (g_fixture) {
        if (load_fixture(g_fixture) != 0) return 1;
    } else {
        load_defaults();
    }

    lv_init();
    lv_tick_set_cb(tick_cb);
    disp = open_display();
    if (!disp) {
        fprintf(stderr, "display: create failed\n");
        return 1;
    }
    lv_tick_set_cb(tick_cb);
    if (g_touch) open_touch(g_touch);
    build_ui();
    if (g_have_api && !g_shot) {
        poll_api();
        next_api = mono_now() + 2.0;
    }
    update_clock();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    for (i = 0; i < 4; i++) lv_timer_handler();

    if (g_shot) {
        int rc = g_shot_status == 1 ? 0 : 1;
        if (rc != 0) fprintf(stderr, "shot: no frame written\n");
        print_stats();
        return rc;
    }

    while (!g_stop) {
        uint32_t wait_ms;
        if (g_duration >= 0 && (mono_now() - g_main_mono) >= (double)g_duration) break;
        if (g_have_api && mono_now() >= next_api) {
            poll_api();
            next_api = mono_now() + 2.0;
        }
        update_clock();
        wait_ms = lv_timer_handler();
        sleep_ms(wait_ms);
    }

    print_stats();
    if (g_tfd >= 0) close(g_tfd);
    return 0;
}
