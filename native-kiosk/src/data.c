#include "data.h"

#include "zk_actions.h"
#include "zk_http.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Defined strongly in platform.c. Absent in host tests, so the call is skipped. */
extern void zk_platform_wake(void) __attribute__((weak));

enum {
    ACT_Q = 8,
    BODY_MAX = 768,
    BASE_MAX = 128,
    SLOW_POLL_MS = 30000,
    HTTP_TIMEOUT_MS = 5000
};

typedef struct {
    uint32_t seq;
    zk_act_kind_t kind;
    char body[BODY_MAX];
} qitem_t;

typedef struct {
    zk_status_t status;
    int have_status;
    int64_t status_mono_ms;
    int have_mono;
    zk_stations_t stations;
    zk_schedules_t schedules;
    zk_soil_t soil;
    int have_stations;
    int have_schedules;
    int have_soil;
} store_t;

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_mu;
static pthread_cond_t g_cv; /* poll thread */
static pthread_cond_t g_act_cv; /* action thread */
static pthread_t g_th;
static pthread_t g_act_th;

static store_t g_snap;
static uint32_t g_version;
static int g_wall_seen;
static zk_wall_t g_wall_pub;
static int g_stale_seen;
static int g_stale_pub;

static qitem_t g_q[ACT_Q];
static int g_q_head;
static int g_q_len;
static uint32_t g_seq;
static uint32_t g_latest;
static zk_action_result_t g_res;
static int g_force_status;

static int g_stop;
static int g_running;
static int g_poll_alive;
static int g_act_alive;
static int g_inflight;
static uint32_t g_inflight_seq;
static zk_act_kind_t g_inflight_kind;
static char g_inflight_body[BODY_MAX];
static int g_fixture;
static int g_force_stale; /* shots only */
static int g_allow_writes;
static int g_live_clock;
static char g_base[BASE_MAX];

static int g_wake_r = -1;
static int g_wake_w = -1;

static int64_t mono_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

static void mono_deadline(int64_t delta_ms, struct timespec *ts)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    if (delta_ms < 0) {
        delta_ms = 0;
    }
    ts->tv_sec += delta_ms / 1000;
    ts->tv_nsec += (long)(delta_ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

/* Tests override the 2s status cadence. Other polls stay at 30s. */
static int status_poll_ms(void)
{
    const char *e = getenv("ZK_POLL_MS_STATUS");
    char *end = NULL;
    long v;
    if (!e || !e[0]) {
        return 2000;
    }
    v = strtol(e, &end, 10);
    if (end == e || (end && *end) || v < 1 || v > 600000) {
        return 2000;
    }
    return (int)v;
}

static void arm_wake_locked(void)
{
    char b = 1;
    ssize_t n;
    if (g_wake_w < 0) {
        return;
    }
    n = write(g_wake_w, &b, 1);
    (void)n;
}

static void call_platform_wake(void)
{
    void (*fn)(void) = zk_platform_wake;
    if (fn) {
        fn();
    }
}

static void init_sync(void)
{
    pthread_condattr_t attr;
    int fds[2];
    int fl;

    pthread_mutex_init(&g_mu, NULL);
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&g_cv, &attr);
    pthread_cond_init(&g_act_cv, &attr);
    pthread_condattr_destroy(&attr);
    if (pipe(fds) != 0) {
        g_wake_r = -1;
        g_wake_w = -1;
        return;
    }
    fl = fcntl(fds[0], F_GETFL, 0);
    if (fl >= 0) {
        fcntl(fds[0], F_SETFL, fl | O_NONBLOCK);
    }
    fl = fcntl(fds[1], F_GETFL, 0);
    if (fl >= 0) {
        fcntl(fds[1], F_SETFL, fl | O_NONBLOCK);
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    g_wake_r = fds[0];
    g_wake_w = fds[1];
}

static void ensure_init(void)
{
    pthread_once(&g_once, init_sync);
}

static void reset_locked(void)
{
    memset(&g_snap, 0, sizeof g_snap);
    g_version = 0;
    g_wall_seen = 0;
    memset(&g_wall_pub, 0, sizeof g_wall_pub);
    g_stale_seen = 0;
    g_stale_pub = 0;
    g_q_head = 0;
    g_q_len = 0;
    g_seq = 0;
    g_latest = 0;
    memset(&g_res, 0, sizeof g_res);
    g_force_status = 0;
    g_stop = 0;
    g_running = 0;
    g_poll_alive = 0;
    g_act_alive = 0;
    g_inflight = 0;
    g_inflight_seq = 0;
    g_inflight_body[0] = 0;
    g_fixture = 0;
    g_allow_writes = 0;
    g_live_clock = 0;
    g_base[0] = 0;
}

static void remember_status(const zk_status_t *st, int64_t mono)
{
    int changed = !g_snap.have_status || memcmp(&g_snap.status, st, sizeof *st) != 0;
    g_snap.status = *st;
    g_snap.have_status = 1;
    g_snap.status_mono_ms = mono;
    g_snap.have_mono = 1;
    if (changed) {
        g_version++;
    }
}

static void remember_stations(const zk_stations_t *v)
{
    int changed = !g_snap.have_stations || memcmp(&g_snap.stations, v, sizeof *v) != 0;
    g_snap.stations = *v;
    g_snap.have_stations = 1;
    if (changed) {
        g_version++;
    }
}

static void remember_schedules(const zk_schedules_t *v)
{
    int changed = !g_snap.have_schedules || memcmp(&g_snap.schedules, v, sizeof *v) != 0;
    g_snap.schedules = *v;
    g_snap.have_schedules = 1;
    if (changed) {
        g_version++;
    }
}

static void remember_soil(const zk_soil_t *v)
{
    int changed = !g_snap.have_soil || memcmp(&g_snap.soil, v, sizeof *v) != 0;
    g_snap.soil = *v;
    g_snap.have_soil = 1;
    if (changed) {
        g_version++;
    }
}

static int flag_stop(void)
{
    int s;
    pthread_mutex_lock(&g_mu);
    s = g_stop;
    pthread_mutex_unlock(&g_mu);
    return s;
}

/* Caller must not hold g_mu. A failed exchange leaves the previous snapshot. */
static int fetch(char *buf, const char *path)
{
    char base[BASE_MAX];
    int st = 0;
    int rc;

    pthread_mutex_lock(&g_mu);
    if (g_stop) {
        pthread_mutex_unlock(&g_mu);
        return ZK_ERR_ARG;
    }
    memcpy(base, g_base, sizeof base);
    pthread_mutex_unlock(&g_mu);
    if (!base[0]) {
        return ZK_ERR_ARG;
    }
    rc = zk_http_get(base, path, HTTP_TIMEOUT_MS, buf, ZK_HTTP_MAX_BODY, &st);
    if (rc != ZK_OK) {
        return rc;
    }
    if (st < 200 || st >= 300) {
        return ZK_ERR_HTTP;
    }
    return ZK_OK;
}

static void poll_status(char *buf)
{
    zk_status_t st;
    if (fetch(buf, "/api/status") != ZK_OK) {
        return;
    }
    if (zk_parse_status(buf, &st) != ZK_OK) {
        return;
    }
    pthread_mutex_lock(&g_mu);
    remember_status(&st, mono_ms());
    arm_wake_locked();
    pthread_mutex_unlock(&g_mu);
    call_platform_wake();
}

static void poll_stations(char *buf)
{
    zk_stations_t v;
    if (fetch(buf, "/api/stations") != ZK_OK) {
        return;
    }
    if (zk_parse_stations(buf, &v) != ZK_OK) {
        return;
    }
    pthread_mutex_lock(&g_mu);
    remember_stations(&v);
    arm_wake_locked();
    pthread_mutex_unlock(&g_mu);
    call_platform_wake();
}

static void poll_schedules(char *buf)
{
    zk_schedules_t v;
    if (fetch(buf, "/api/schedules") != ZK_OK) {
        return;
    }
    if (zk_parse_schedules(buf, &v) != ZK_OK) {
        return;
    }
    pthread_mutex_lock(&g_mu);
    remember_schedules(&v);
    arm_wake_locked();
    pthread_mutex_unlock(&g_mu);
    call_platform_wake();
}

static void poll_soil(char *buf)
{
    zk_soil_t v;
    if (fetch(buf, "/api/soil") != ZK_OK) {
        return;
    }
    if (zk_parse_soil(buf, &v) != ZK_OK) {
        return;
    }
    pthread_mutex_lock(&g_mu);
    remember_soil(&v);
    arm_wake_locked();
    pthread_mutex_unlock(&g_mu);
    call_platform_wake();
}

static int dispatch_action(int allow, const char *base, zk_act_kind_t kind, const char *body, int *http_status)
{
    zk_actions_t a;
    memset(&a, 0, sizeof a);
    a.allow_writes = allow;
    zk_str_copy(a.base, sizeof a.base, base);
    if (http_status) {
        *http_status = 0;
    }
    switch (kind) {
    case ZK_ACT_STOP:
        return zk_action_stop(&a, http_status);
    case ZK_ACT_PAUSE:
        return zk_action_pause(&a, body, http_status);
    case ZK_ACT_RESUME:
        return zk_action_resume(&a, http_status);
    default:
        return ZK_ERR_ARG;
    }
}

static void publish_result(const qitem_t *job, int code, int http_status, int repoll)
{
    pthread_mutex_lock(&g_mu);
    {
        /* Coalesced submits move g_inflight_seq. The copy in job stays at dequeue. */
        uint32_t seq = job->seq;
        if (g_inflight) {
            seq = g_inflight_seq;
            g_inflight = 0;
            g_inflight_seq = 0;
        }
        if (seq == g_latest) {
            g_res.pending = 0;
            g_res.done = 1;
            g_res.code = code;
            g_res.http_status = http_status;
            g_res.kind = job->kind;
            g_res.seq = seq;
        }
        if (repoll) {
            g_force_status = 1;
            pthread_cond_signal(&g_cv);
        }
        arm_wake_locked();
    }
    pthread_mutex_unlock(&g_mu);
    call_platform_wake();
}

static int act_same(zk_act_kind_t ak, const char *abody, zk_act_kind_t bk, const char *bbody)
{
    if (ak != bk) {
        return 0;
    }
    if (ak == ZK_ACT_PAUSE) {
        if (!abody) {
            abody = "";
        }
        if (!bbody) {
            bbody = "";
        }
        return strcmp(abody, bbody) == 0;
    }
    return ak == ZK_ACT_STOP || ak == ZK_ACT_RESUME;
}

/* A repeat of the queued or in-flight job is not posted again.
 * seq is the submit the UI is waiting on; publish reports that one result. */
static int coalesce_locked(zk_act_kind_t kind, const char *body, uint32_t seq)
{
    int i;
    if (g_inflight && act_same(g_inflight_kind, g_inflight_body, kind, body)) {
        g_inflight_seq = seq;
        return 1;
    }
    for (i = 0; i < g_q_len; i++) {
        qitem_t *it = &g_q[(g_q_head + i) % ACT_Q];
        if (!act_same(it->kind, it->body, kind, body)) {
            continue;
        }
        it->seq = seq;
        return 1;
    }
    return 0;
}

static void run_job(const qitem_t *job);

static void pend_locked(zk_act_kind_t kind, uint32_t seq)
{
    g_res.pending = 1;
    g_res.done = 0;
    g_res.code = 0;
    g_res.http_status = 0;
    g_res.kind = kind;
    g_res.seq = seq;
}

/* POSTs only. Sockets are opened inside zk_actions / zk_http for this thread. */
static void *action_main(void *arg)
{
    (void)arg;
    for (;;) {
        qitem_t job;

        pthread_mutex_lock(&g_mu);
        while (!g_stop && g_q_len == 0) {
            pthread_cond_wait(&g_act_cv, &g_mu);
        }
        if (g_stop) {
            pthread_mutex_unlock(&g_mu);
            break;
        }
        job = g_q[g_q_head];
        g_q_head = (g_q_head + 1) % ACT_Q;
        g_q_len--;
        g_inflight = 1;
        g_inflight_kind = job.kind;
        memcpy(g_inflight_body, job.body, sizeof g_inflight_body);
        g_inflight_seq = job.seq;
        pthread_mutex_unlock(&g_mu);
        run_job(&job);
    }
    return NULL;
}

static void run_job(const qitem_t *job)
{
    char base[BASE_MAX];
    int allow;
    int st = 0;
    int rc;

    pthread_mutex_lock(&g_mu);
    allow = g_allow_writes;
    memcpy(base, g_base, sizeof base);
    pthread_mutex_unlock(&g_mu);
    rc = dispatch_action(allow, base, job->kind, job->body, &st);
    publish_result(job, rc, st, rc == ZK_OK);
}

/* GETs only. Does not dequeue actions and does not share a socket with them. */
static void *worker_main(void *arg)
{
    int status_ms = status_poll_ms();
    int64_t next_status = 0;
    int64_t next_slow = 0;
    char *buf;

    (void)arg;
    buf = (char *)malloc(ZK_HTTP_MAX_BODY);
    if (!buf) {
        return NULL;
    }
    for (;;) {
        int do_status = 0;
        int do_slow = 0;

        pthread_mutex_lock(&g_mu);
        if (g_stop) {
            pthread_mutex_unlock(&g_mu);
            break;
        }
        {
            int64_t now = mono_ms();
            if (g_force_status || now >= next_status) {
                g_force_status = 0;
                do_status = 1;
            } else if (now >= next_slow) {
                do_slow = 1;
            } else {
                int64_t wait_ms = (next_status < next_slow ? next_status : next_slow) - now;
                struct timespec dl;
                if (wait_ms < 0) {
                    wait_ms = 0;
                }
                mono_deadline(wait_ms, &dl);
                pthread_cond_timedwait(&g_cv, &g_mu, &dl);
                pthread_mutex_unlock(&g_mu);
                continue;
            }
        }
        pthread_mutex_unlock(&g_mu);

        if (do_status) {
            poll_status(buf);
            next_status = mono_ms() + status_ms;
            continue;
        }
        if (do_slow) {
            poll_stations(buf);
            if (!flag_stop()) {
                poll_schedules(buf);
            }
            if (!flag_stop()) {
                poll_soil(buf);
            }
            next_slow = mono_ms() + SLOW_POLL_MS;
        }
    }
    free(buf);
    return NULL;
}

static int load_named(const char *dir, const char *name, char **out)
{
    char path[768];
    FILE *f;
    long n;
    char *b;
    int wr;

    *out = NULL;
    wr = snprintf(path, sizeof path, "%s/%s", dir, name);
    if (wr < 0 || (size_t)wr >= sizeof path) {
        return ZK_ERR_OVERFLOW;
    }
    f = fopen(path, "rb");
    if (!f) {
        return ZK_ERR_IO;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ZK_ERR_IO;
    }
    n = ftell(f);
    if (n < 0 || n >= ZK_HTTP_MAX_BODY) {
        fclose(f);
        return ZK_ERR_OVERFLOW;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return ZK_ERR_IO;
    }
    b = (char *)malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return ZK_ERR_IO;
    }
    if (n > 0 && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return ZK_ERR_IO;
    }
    b[n] = 0;
    fclose(f);
    *out = b;
    return ZK_OK;
}

/* fixture_dir wins: never open a socket for a fixture run. */
static void load_fixture(const char *dir)
{
    char *status_b = NULL;
    char *stations_b = NULL;
    char *sched_b = NULL;
    char *soil_b = NULL;
    zk_status_t st;
    zk_stations_t stations;
    zk_schedules_t schedules;
    zk_soil_t soil;
    int ok_st = 0;
    int ok_sta = 0;
    int ok_sch = 0;
    int ok_soil = 0;
    int64_t mono;

    memset(&st, 0, sizeof st);
    memset(&stations, 0, sizeof stations);
    memset(&schedules, 0, sizeof schedules);
    memset(&soil, 0, sizeof soil);
    if (load_named(dir, "status.json", &status_b) == ZK_OK && zk_parse_status(status_b, &st) == ZK_OK) {
        ok_st = 1;
    }
    if (load_named(dir, "stations.json", &stations_b) == ZK_OK && zk_parse_stations(stations_b, &stations) == ZK_OK) {
        ok_sta = 1;
    }
    if (load_named(dir, "schedules.json", &sched_b) == ZK_OK && zk_parse_schedules(sched_b, &schedules) == ZK_OK) {
        ok_sch = 1;
    }
    if (load_named(dir, "soil.json", &soil_b) == ZK_OK && zk_parse_soil(soil_b, &soil) == ZK_OK) {
        ok_soil = 1;
    }
    mono = mono_ms();
    pthread_mutex_lock(&g_mu);
    if (ok_st) {
        remember_status(&st, mono);
    }
    if (ok_sta) {
        remember_stations(&stations);
    }
    if (ok_sch) {
        remember_schedules(&schedules);
    }
    if (ok_soil) {
        remember_soil(&soil);
    }
    if (ok_st || ok_sta || ok_sch || ok_soil) {
        arm_wake_locked();
    }
    pthread_mutex_unlock(&g_mu);
    if (ok_st || ok_sta || ok_sch || ok_soil) {
        call_platform_wake();
    }
    free(status_b);
    free(stations_b);
    free(sched_b);
    free(soil_b);
}

static void fail_submit(zk_act_kind_t kind, int code)
{
    pthread_mutex_lock(&g_mu);
    g_seq++;
    g_latest = g_seq;
    g_res.pending = 0;
    g_res.done = 1;
    g_res.code = code;
    g_res.http_status = 0;
    g_res.kind = kind;
    g_res.seq = g_seq;
    pthread_mutex_unlock(&g_mu);
}

static int wall_changed(const zk_wall_t *a, const zk_wall_t *b)
{
    /* Minute/second, plus coarser fields so an hour jump that lands on the same mm:ss still counts. */
    return a->ss != b->ss || a->mm != b->mm || a->hh != b->hh || a->d != b->d || a->m != b->m || a->y != b->y;
}

int zk_data_start(const zk_app_t *app)
{
    int fixture;
    int rc;

    ensure_init();
    if (!app) {
        return ZK_ERR_ARG;
    }
    zk_data_stop();
    fixture = app->fixture_dir && app->fixture_dir[0];
    pthread_mutex_lock(&g_mu);
    reset_locked();
    g_allow_writes = app->allow_writes ? 1 : 0;
    g_live_clock = app->live_clock ? 1 : 0;
    if (fixture) {
        g_fixture = 1;
        g_running = 1;
        pthread_mutex_unlock(&g_mu);
        load_fixture(app->fixture_dir);
        return ZK_OK;
    }
    if (!app->api || !app->api[0]) {
        pthread_mutex_unlock(&g_mu);
        return ZK_ERR_ARG;
    }
    if (strlen(app->api) >= sizeof g_base) {
        pthread_mutex_unlock(&g_mu);
        return ZK_ERR_OVERFLOW;
    }
    zk_str_copy(g_base, sizeof g_base, app->api);
    g_fixture = 0;
    g_running = 1;
    g_stop = 0;
    g_poll_alive = 1;
    g_act_alive = 1;
    pthread_mutex_unlock(&g_mu);
    rc = pthread_create(&g_th, NULL, worker_main, NULL);
    if (rc != 0) {
        pthread_mutex_lock(&g_mu);
        g_running = 0;
        g_poll_alive = 0;
        g_act_alive = 0;
        pthread_mutex_unlock(&g_mu);
        return ZK_ERR_IO;
    }
    rc = pthread_create(&g_act_th, NULL, action_main, NULL);
    if (rc != 0) {
        pthread_mutex_lock(&g_mu);
        g_stop = 1;
        g_act_alive = 0;
        pthread_cond_broadcast(&g_cv);
        pthread_cond_broadcast(&g_act_cv);
        pthread_mutex_unlock(&g_mu);
        pthread_join(g_th, NULL);
        pthread_mutex_lock(&g_mu);
        g_poll_alive = 0;
        g_running = 0;
        pthread_mutex_unlock(&g_mu);
        return ZK_ERR_IO;
    }
    return ZK_OK;
}

void zk_data_stop(void)
{
    int poll_alive;
    int act_alive;

    ensure_init();
    pthread_mutex_lock(&g_mu);
    g_stop = 1;
    poll_alive = g_poll_alive;
    act_alive = g_act_alive;
    pthread_cond_broadcast(&g_cv);
    pthread_cond_broadcast(&g_act_cv);
    pthread_mutex_unlock(&g_mu);
    if (poll_alive) {
        pthread_join(g_th, NULL);
    }
    if (act_alive) {
        pthread_join(g_act_th, NULL);
    }
    pthread_mutex_lock(&g_mu);
    g_poll_alive = 0;
    g_act_alive = 0;
    g_running = 0;
    g_q_head = 0;
    g_q_len = 0;
    g_force_status = 0;
    g_inflight = 0;
    g_inflight_seq = 0;
    pthread_mutex_unlock(&g_mu);
}

void zk_data_get(zk_snapshot_t *out)
{
    int64_t now;

    if (!out) {
        return;
    }
    ensure_init();
    now = mono_ms();
    pthread_mutex_lock(&g_mu);
    memset(out, 0, sizeof *out);
    out->status = g_snap.status;
    out->have_status = g_snap.have_status;
    out->stations = g_snap.stations;
    out->schedules = g_snap.schedules;
    out->soil = g_snap.soil;
    out->have_stations = g_snap.have_stations;
    out->have_schedules = g_snap.have_schedules;
    out->have_soil = g_snap.have_soil;
    if (!g_snap.have_mono) {
        out->status_age_s = 1.0e9;
    } else {
        double age = (double)(now - g_snap.status_mono_ms) / 1000.0;
        if (age < 0) {
            age = 0;
        }
        out->status_age_s = age;
    }
    if (g_fixture) {
        out->status_age_s = g_force_stale ? 1.0e9 : 0; /* a canned fixture is never stale */
    }
    out->stale = out->status_age_s > 6.0 ? 1 : 0;
    if (g_snap.have_status && g_snap.status.has_now) {
        if (g_fixture && !g_live_clock) {
            out->wall = g_snap.status.now;
        } else if (g_snap.have_mono) {
            out->wall = zk_wall_advance(g_snap.status.now, now, g_snap.status_mono_ms);
        } else {
            out->wall = g_snap.status.now;
        }
        if (g_wall_seen && wall_changed(&g_wall_pub, &out->wall)) {
            g_version++;
        }
        g_wall_pub = out->wall;
        g_wall_seen = 1;
    }
    if (g_stale_seen && out->stale != g_stale_pub) {
        g_version++;
    }
    g_stale_pub = out->stale;
    g_stale_seen = 1;
    out->version = g_version;
    pthread_mutex_unlock(&g_mu);
}

int zk_data_wake_fd(void)
{
    ensure_init();
    return g_wake_r;
}

void zk_data_submit_action(zk_act_kind_t kind, const char *json_body)
{
    qitem_t item;
    int fixture;
    int allow;
    int running;

    ensure_init();
    if (kind != ZK_ACT_STOP && kind != ZK_ACT_PAUSE && kind != ZK_ACT_RESUME) {
        fail_submit(kind, ZK_ERR_ARG);
        return;
    }
    if (json_body && strlen(json_body) >= BODY_MAX) {
        fail_submit(kind, ZK_ERR_OVERFLOW);
        return;
    }
    memset(&item, 0, sizeof item);
    item.kind = kind;
    zk_str_copy(item.body, sizeof item.body, json_body);

    pthread_mutex_lock(&g_mu);
    g_seq++;
    item.seq = g_seq;
    g_latest = g_seq;
    fixture = g_fixture;
    allow = g_allow_writes;
    running = g_running;
    if (!running) {
        g_res.pending = 0;
        g_res.done = 1;
        g_res.code = ZK_ERR_ARG;
        g_res.http_status = 0;
        g_res.kind = kind;
        g_res.seq = g_seq;
        pthread_mutex_unlock(&g_mu);
        return;
    }
    if (fixture) {
        pthread_mutex_unlock(&g_mu);
        {
            int st = 0;
            int rc;
            /* Writes in fixture mode succeed locally. There is no base and no socket. */
            if (allow) {
                rc = ZK_OK;
            } else {
                rc = dispatch_action(0, "", kind, item.body, &st);
            }
            publish_result(&item, rc, st, 0);
        }
        return;
    }
    if (coalesce_locked(kind, item.body, item.seq)) {
        pend_locked(kind, item.seq);
        pthread_mutex_unlock(&g_mu);
        return;
    }
    if (g_q_len >= ACT_Q) {
        g_res.pending = 0;
        g_res.done = 1;
        g_res.code = ZK_ERR_OVERFLOW;
        g_res.http_status = 0;
        g_res.kind = kind;
        g_res.seq = g_seq;
        pthread_mutex_unlock(&g_mu);
        return;
    }
    g_q[(g_q_head + g_q_len) % ACT_Q] = item;
    g_q_len++;
    pend_locked(kind, item.seq);
    pthread_cond_signal(&g_act_cv);
    pthread_mutex_unlock(&g_mu);
}

void zk_data_action_result(zk_action_result_t *out)
{
    if (!out) {
        return;
    }
    ensure_init();
    pthread_mutex_lock(&g_mu);
    *out = g_res;
    pthread_mutex_unlock(&g_mu);
}

void zk_data_debug_force_stale(int on)
{
    pthread_mutex_lock(&g_mu);
    g_force_stale = on ? 1 : 0;
    pthread_mutex_unlock(&g_mu);
}
