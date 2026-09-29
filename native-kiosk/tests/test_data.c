#include "zk_test.h"
#include "data.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int listen_fd;
    int port;
    pthread_t th;
    int stop;
    int delay_ms;
    pthread_mutex_t mu;
    int n_status;
    int n_stations;
    int n_sched;
    int n_soil;
    int n_post;
    int n_cancel;
    int n_pause;
    int n_resume;
    char last_req[512];
    char last_body[512];
    char last_post_req[512];
    char last_post_body[512];
} stub_t;

typedef struct {
    int n_status;
    int n_stations;
    int n_sched;
    int n_soil;
    int n_post;
    int n_cancel;
    int n_pause;
    int n_resume;
    char last_req[512];
    char last_body[512];
    char last_post_req[512];
    char last_post_body[512];
} counts_t;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int thread_count(void)
{
    FILE *f = fopen("/proc/self/status", "re");
    char line[256];
    int n = -1;
    if (!f) {
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "Threads: %d", &n) == 1) {
            break;
        }
    }
    fclose(f);
    return n;
}

static int count_sockets(void)
{
    DIR *d = opendir("/proc/self/fd");
    struct dirent *e;
    int n = 0;
    if (!d) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char path[96];
        char link[128];
        ssize_t k;
        int wr;
        if (e->d_name[0] == '.') {
            continue;
        }
        wr = snprintf(path, sizeof path, "/proc/self/fd/%s", e->d_name);
        if (wr < 0 || (size_t)wr >= sizeof path) {
            continue;
        }
        k = readlink(path, link, sizeof link - 1);
        if (k < 0) {
            continue;
        }
        link[k] = 0;
        if (strncmp(link, "socket:", 7) == 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

static void send_all(int fd, const char *p, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(fd, p + off, n - off, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (w == 0) {
            break;
        }
        off += (size_t)w;
    }
}

static int req_is(const char *req, const char *method, const char *path)
{
    size_t ml = strlen(method);
    size_t pl = strlen(path);
    char c;
    if (strncmp(req, method, ml) != 0 || req[ml] != ' ') {
        return 0;
    }
    if (strncmp(req + ml + 1, path, pl) != 0) {
        return 0;
    }
    c = req[ml + 1 + pl];
    return c == ' ' || c == '\0' || c == '?';
}

static void reply_json(int fd, const char *json)
{
    char hdr[192];
    int m = snprintf(hdr, sizeof hdr,
                     "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                     strlen(json));
    if (m > 0) {
        send_all(fd, hdr, (size_t)m);
        send_all(fd, json, strlen(json));
    }
}

static void sleep_ms_flag(stub_t *s, int ms)
{
    int left = ms;
    while (left > 0 && !__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
        int slice = left > 20 ? 20 : left;
        usleep((useconds_t)slice * 1000);
        left -= slice;
    }
}

static int read_req(int fd, char *buf, size_t cap, size_t *n, char *body, size_t body_cap)
{
    size_t got = 0;
    char *sep;
    long clen = 0;
    *n = 0;
    if (body && body_cap) {
        body[0] = 0;
    }
    while (got + 1 < cap) {
        ssize_t r = recv(fd, buf + got, cap - 1 - got, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            break;
        }
        got += (size_t)r;
        buf[got] = 0;
        if (strstr(buf, "\r\n\r\n")) {
            break;
        }
    }
    buf[got] = 0;
    *n = got;
    sep = strstr(buf, "\r\n\r\n");
    if (!sep || !body || body_cap == 0) {
        return 0;
    }
    {
        const char *p = buf;
        while (*p) {
            if ((p == buf || p[-1] == '\n') &&
                (strncmp(p, "Content-Length:", 15) == 0 || strncmp(p, "content-length:", 15) == 0)) {
                clen = strtol(p + 15, NULL, 10);
                break;
            }
            p = strstr(p, "\r\n");
            if (!p) {
                break;
            }
            p += 2;
        }
    }
    {
        size_t hdr_end = (size_t)(sep + 4 - buf);
        size_t have = got > hdr_end ? got - hdr_end : 0;
        size_t need = clen > 0 ? (size_t)clen : 0;
        size_t bl = 0;
        if (have > 0) {
            if (have > body_cap - 1) {
                have = body_cap - 1;
            }
            memcpy(body, sep + 4, have);
            bl = have;
        }
        while (bl < need && bl + 1 < body_cap) {
            size_t room = body_cap - 1 - bl;
            size_t chunk = need - bl < room ? need - bl : room;
            ssize_t r = recv(fd, body + bl, chunk, 0);
            if (r <= 0) {
                break;
            }
            bl += (size_t)r;
        }
        body[bl] = 0;
    }
    return 0;
}

static void *stub_main(void *arg)
{
    stub_t *s = (stub_t *)arg;
    while (!__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
        struct timeval tv;
        fd_set rfds;
        int cfd;
        char req[2048];
        char body[512];
        size_t n = 0;
        int delay;
        const char *json = "{}";
        FD_ZERO(&rfds);
        FD_SET(s->listen_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        if (select(s->listen_fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
            continue;
        }
        if (__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
            break;
        }
        cfd = accept(s->listen_fd, NULL, NULL);
        if (cfd < 0) {
            continue;
        }
        if (read_req(cfd, req, sizeof req, &n, body, sizeof body) != 0) {
            close(cfd);
            continue;
        }
        pthread_mutex_lock(&s->mu);
        zk_str_copy(s->last_req, sizeof s->last_req, req);
        zk_str_copy(s->last_body, sizeof s->last_body, body);
        if (req_is(req, "GET", "/api/status")) {
            s->n_status++;
            json = (s->n_status <= 1)
                       ? "{\"phase\":\"Idle\",\"now\":\"2026-09-29T06:52:00-06:00\"}"
                       : "{\"phase\":\"Run\",\"now\":\"2026-09-29T06:52:05-06:00\"}";
        } else if (req_is(req, "GET", "/api/stations")) {
            s->n_stations++;
            json = "{\"stations\":[{\"id\":\"front\",\"title\":\"Front\",\"color\":\"red\"}]}";
        } else if (req_is(req, "GET", "/api/schedules")) {
            s->n_sched++;
            json = "{\"schedules\":[{\"id\":\"am\",\"note\":\"Morning\",\"enabled\":true,\"start\":\"08:00\",\"steps\":[{\"station_id\":\"front\",\"minutes\":2}]}]}";
        } else if (req_is(req, "GET", "/api/soil")) {
            s->n_soil++;
            json = "{\"enabled\":true,\"et_known\":true,\"zones\":[{\"station_id\":\"front\",\"percent\":40,\"rate_measured\":true}]}";
        } else if (req_is(req, "POST", "/api/run/cancel")) {
            s->n_post++;
            s->n_cancel++;
            zk_str_copy(s->last_post_req, sizeof s->last_post_req, req);
            zk_str_copy(s->last_post_body, sizeof s->last_post_body, body);
            json = "{}";
        } else if (req_is(req, "POST", "/api/pause/resume")) {
            s->n_post++;
            s->n_resume++;
            zk_str_copy(s->last_post_req, sizeof s->last_post_req, req);
            zk_str_copy(s->last_post_body, sizeof s->last_post_body, body);
            json = "{}";
        } else if (req_is(req, "POST", "/api/pause")) {
            s->n_post++;
            s->n_pause++;
            zk_str_copy(s->last_post_req, sizeof s->last_post_req, req);
            zk_str_copy(s->last_post_body, sizeof s->last_post_body, body);
            json = "{}";
        } else if (strncmp(req, "POST ", 5) == 0) {
            s->n_post++;
            zk_str_copy(s->last_post_req, sizeof s->last_post_req, req);
            zk_str_copy(s->last_post_body, sizeof s->last_post_body, body);
            json = "{}";
        } else if (strncmp(req, "GET ", 4) == 0) {
            json = "{}";
        }
        pthread_mutex_unlock(&s->mu);
        delay = __atomic_load_n(&s->delay_ms, __ATOMIC_ACQUIRE);
        if (delay > 0) {
            sleep_ms_flag(s, delay);
        }
        if (!__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
            reply_json(cfd, json);
        }
        close(cfd);
    }
    return NULL;
}

static int stub_start(stub_t *s, int delay_ms)
{
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    int on = 1;
    memset(s, 0, sizeof *s);
    pthread_mutex_init(&s->mu, NULL);
    __atomic_store_n(&s->delay_ms, delay_ms, __ATOMIC_RELEASE);
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) {
        return -1;
    }
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        listen(s->listen_fd, 8) < 0 ||
        getsockname(s->listen_fd, (struct sockaddr *)&addr, &alen) < 0) {
        close(s->listen_fd);
        pthread_mutex_destroy(&s->mu);
        return -1;
    }
    s->port = ntohs(addr.sin_port);
    if (pthread_create(&s->th, NULL, stub_main, s) != 0) {
        close(s->listen_fd);
        pthread_mutex_destroy(&s->mu);
        return -1;
    }
    return 0;
}

static void stub_stop(stub_t *s)
{
    __atomic_store_n(&s->stop, 1, __ATOMIC_RELEASE);
    shutdown(s->listen_fd, SHUT_RDWR);
    close(s->listen_fd);
    pthread_join(s->th, NULL);
    pthread_mutex_destroy(&s->mu);
}

static void stub_counts(stub_t *s, counts_t *c)
{
    pthread_mutex_lock(&s->mu);
    memset(c, 0, sizeof *c);
    c->n_status = s->n_status;
    c->n_stations = s->n_stations;
    c->n_sched = s->n_sched;
    c->n_soil = s->n_soil;
    c->n_post = s->n_post;
    c->n_cancel = s->n_cancel;
    c->n_pause = s->n_pause;
    c->n_resume = s->n_resume;
    zk_str_copy(c->last_req, sizeof c->last_req, s->last_req);
    zk_str_copy(c->last_body, sizeof c->last_body, s->last_body);
    zk_str_copy(c->last_post_req, sizeof c->last_post_req, s->last_post_req);
    zk_str_copy(c->last_post_body, sizeof c->last_post_body, s->last_post_body);
    pthread_mutex_unlock(&s->mu);
}

static int wait_done(zk_action_result_t *r, int timeout_ms)
{
    int64_t t0 = now_ms();
    for (;;) {
        zk_data_action_result(r);
        if (r->done) {
            return 0;
        }
        if (now_ms() - t0 > timeout_ms) {
            return -1;
        }
        usleep(10000);
    }
}

static const char *find_home_rain(void)
{
    static const char *cands[] = {
        "tests/fixtures/home-rain",
        "native-kiosk/tests/fixtures/home-rain",
        "../tests/fixtures/home-rain",
        NULL
    };
    int i;
    for (i = 0; cands[i]; i++) {
        char p[512];
        int wr = snprintf(p, sizeof p, "%s/status.json", cands[i]);
        if (wr > 0 && (size_t)wr < sizeof p && access(p, R_OK) == 0) {
            return cands[i];
        }
    }
    return NULL;
}

static int wall_eq(zk_wall_t a, zk_wall_t b)
{
    return a.y == b.y && a.m == b.m && a.d == b.d && a.hh == b.hh && a.mm == b.mm && a.ss == b.ss &&
           a.wday == b.wday;
}

static void test_initial_and_repoll(void)
{
    stub_t s;
    zk_app_t app;
    zk_snapshot_t snap;
    counts_t c;
    char base[64];
    int64_t t0;
    int i;
    int saw_run = 0;
    uint32_t v0;
    char junk[32];
    ssize_t nr;

    TCHECK(setenv("ZK_POLL_MS_STATUS", "2000", 1) == 0, "setenv");
    TCHECK(stub_start(&s, 0) == 0, "stub");
    snprintf(base, sizeof base, "http://127.0.0.1:%d", s.port);
    memset(&app, 0, sizeof app);
    app.api = base;
    TCHECK(zk_data_start(&app) == 0, "start");
    TCHECK(zk_data_wake_fd() >= 0, "wake fd");
    for (i = 0; i < 150; i++) {
        stub_counts(&s, &c);
        zk_data_get(&snap);
        if (c.n_status >= 1 && c.n_stations >= 1 && c.n_sched >= 1 && c.n_soil >= 1 && snap.have_status &&
            snap.have_stations && snap.have_schedules && snap.have_soil) {
            break;
        }
        usleep(20000);
    }
    stub_counts(&s, &c);
    zk_data_get(&snap);
    TEQ_I(c.n_status, 1);
    TEQ_I(c.n_stations, 1);
    TEQ_I(c.n_sched, 1);
    TEQ_I(c.n_soil, 1);
    TEQ_I(c.n_post, 0);
    TEQ_I(snap.have_status, 1);
    TEQ_S(snap.status.phase, "Idle");
    TEQ_I(snap.stale, 0);
    TCHECK(snap.status_age_s < 6.0, "age %g", snap.status_age_s);
    TEQ_S(snap.stations.items[0].id, "front");
    TEQ_S(snap.schedules.items[0].id, "am");
    TEQ_I(snap.soil.zones[0].percent, 40);
    TCHECK(snap.version >= 1, "version %u", snap.version);
    nr = read(zk_data_wake_fd(), junk, sizeof junk);
    TCHECK(nr > 0, "wake byte %zd", nr);
    v0 = snap.version;
    t0 = now_ms();
    for (i = 0; i < 250; i++) {
        zk_data_get(&snap);
        if (strcmp(snap.status.phase, "Run") == 0) {
            saw_run = 1;
            break;
        }
        usleep(20000);
    }
    TCHECK(saw_run, "second status did not land");
    TCHECK(now_ms() - t0 >= 1200, "second status too soon %ld", (long)(now_ms() - t0));
    TCHECK(now_ms() - t0 < 4500, "second status too late %ld", (long)(now_ms() - t0));
    stub_counts(&s, &c);
    TEQ_I(c.n_stations, 1);
    TEQ_I(c.n_sched, 1);
    TEQ_I(c.n_soil, 1);
    TCHECK(c.n_status >= 2, "status polls %d", c.n_status);
    TCHECK(snap.version > v0, "version %u -> %u", v0, snap.version);
    zk_data_stop();
    stub_stop(&s);
}

static void test_stale_when_stub_stops(void)
{
    stub_t s;
    zk_app_t app;
    zk_snapshot_t snap;
    counts_t c;
    char base[64];
    char phase[ZK_PHASE_MAX];
    char sid[ZK_ID_MAX];
    int i;
    int became = 0;
    int64_t t0;

    TCHECK(setenv("ZK_POLL_MS_STATUS", "200", 1) == 0, "setenv");
    TCHECK(stub_start(&s, 0) == 0, "stub");
    snprintf(base, sizeof base, "http://127.0.0.1:%d", s.port);
    memset(&app, 0, sizeof app);
    app.api = base;
    TCHECK(zk_data_start(&app) == 0, "start");
    t0 = now_ms();
    for (i = 0; i < 100; i++) {
        stub_counts(&s, &c);
        if (c.n_status >= 3 && c.n_stations >= 1) {
            break;
        }
        usleep(20000);
    }
    stub_counts(&s, &c);
    TCHECK(c.n_status >= 3, "short poll got %d status in %ld ms", c.n_status, (long)(now_ms() - t0));
    TCHECK(now_ms() - t0 < 1500, "short poll too slow %ld", (long)(now_ms() - t0));
    zk_data_get(&snap);
    TEQ_I(snap.have_status, 1);
    TEQ_I(snap.stale, 0);
    zk_str_copy(phase, sizeof phase, snap.status.phase);
    zk_str_copy(sid, sizeof sid, snap.stations.items[0].id);
    stub_stop(&s);
    for (i = 0; i < 180; i++) {
        int64_t g0 = now_ms();
        zk_data_get(&snap);
        TCHECK(now_ms() - g0 < 50, "get blocked while polls fail");
        if (snap.stale && snap.status_age_s > 6.0) {
            became = 1;
            break;
        }
        usleep(50000);
    }
    TCHECK(became, "stale age %g flag %d", snap.status_age_s, snap.stale);
    TEQ_I(snap.have_status, 1);
    TEQ_I(snap.have_stations, 1);
    TEQ_S(snap.status.phase, phase);
    TEQ_S(snap.stations.items[0].id, sid);
    zk_data_stop();
}

static void test_fixture(void)
{
    const char *dir = find_home_rain();
    zk_app_t app;
    zk_snapshot_t snap;
    zk_action_result_t res;
    int threads;
    int sockets;
    int64_t t0;

    TCHECK(dir != NULL, "home-rain fixture");
    if (!dir) {
        return;
    }
    threads = thread_count();
    sockets = count_sockets();
    memset(&app, 0, sizeof app);
    app.fixture_dir = dir;
    app.allow_writes = 0;
    app.live_clock = 0;
    TCHECK(zk_data_start(&app) == 0, "fixture start");
    TEQ_I(thread_count(), threads);
    TEQ_I(count_sockets(), sockets);
    zk_data_get(&snap);
    TEQ_I(snap.have_status, 1);
    TEQ_I(snap.have_stations, 1);
    TEQ_I(snap.have_schedules, 1);
    TEQ_I(snap.have_soil, 1);
    TEQ_S(snap.status.phase, "Idle");
    TEQ_I(snap.status.has_now, 1);
    TEQ_S(snap.status.timezone, "UTC");
    TEQ_I(snap.stations.n, 4);
    TEQ_S(snap.stations.items[0].id, "az01");
    TEQ_S(snap.stations.items[0].title, "Test Station 1");
    TEQ_I(snap.schedules.n, 2);
    TEQ_S(snap.schedules.items[0].id, "morning");
    TEQ_I(snap.soil.enabled, 1);
    TEQ_I(snap.soil.n_zones, 4);
    TEQ_I(snap.soil.zones[0].percent, 58);
    TCHECK(wall_eq(snap.wall, snap.status.now), "frozen wall %04d-%02d-%02d %02d:%02d:%02d vs status",
           snap.wall.y, snap.wall.m, snap.wall.d, snap.wall.hh, snap.wall.mm, snap.wall.ss);
    TCHECK(snap.status_age_s < 2.0, "fixture age %g", snap.status_age_s);
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    zk_data_action_result(&res);
    TEQ_I(res.done, 1);
    TEQ_I(res.pending, 0);
    TEQ_I(res.code, ZK_RO);
    TEQ_I(count_sockets(), sockets);
    zk_data_stop();

    memset(&app, 0, sizeof app);
    app.fixture_dir = dir;
    app.allow_writes = 1;
    TCHECK(zk_data_start(&app) == 0, "fixture writes");
    TEQ_I(thread_count(), threads);
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    zk_data_action_result(&res);
    TEQ_I(res.done, 1);
    TEQ_I(res.code, ZK_OK);
    TEQ_I(res.http_status, 0);
    TEQ_I(count_sockets(), sockets);
    TEQ_I(thread_count(), threads);
    zk_data_stop();

    memset(&app, 0, sizeof app);
    app.fixture_dir = dir;
    app.live_clock = 1;
    TCHECK(zk_data_start(&app) == 0, "live clock");
    TEQ_I(thread_count(), threads);
    t0 = now_ms();
    while (now_ms() - t0 < 1200) {
        usleep(20000);
    }
    zk_data_get(&snap);
    TCHECK(zk_wall_epoch_sec(snap.wall) >= zk_wall_epoch_sec(snap.status.now) + 1, "live wall did not advance");
    TEQ_I(thread_count(), threads);
    zk_data_stop();
    TEQ_I(thread_count(), threads);
}

static void test_actions(void)
{
    stub_t s;
    zk_app_t app;
    zk_snapshot_t snap;
    zk_action_result_t res;
    counts_t c;
    counts_t c1;
    char base[64];
    int i;

    TCHECK(setenv("ZK_POLL_MS_STATUS", "10000", 1) == 0, "setenv");
    TCHECK(stub_start(&s, 0) == 0, "stub");
    snprintf(base, sizeof base, "http://127.0.0.1:%d", s.port);
    memset(&app, 0, sizeof app);
    app.api = base;
    app.allow_writes = 0;
    TCHECK(zk_data_start(&app) == 0, "start ro");
    for (i = 0; i < 100; i++) {
        zk_data_get(&snap);
        if (snap.have_status) {
            break;
        }
        usleep(20000);
    }
    TEQ_I(snap.have_status, 1);
    stub_counts(&s, &c);
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    TCHECK(wait_done(&res, 2000) == 0, "ro result");
    TEQ_I(res.code, ZK_RO);
    TEQ_I(res.http_status, 0);
    TEQ_I(res.kind, ZK_ACT_STOP);
    TCHECK(res.seq >= 1, "seq %u", res.seq);
    usleep(150000);
    stub_counts(&s, &c1);
    TEQ_I(c1.n_post, 0);
    TEQ_I(c1.n_cancel, 0);
    TEQ_I(c1.n_status, c.n_status);
    zk_data_stop();

    memset(&app, 0, sizeof app);
    app.api = base;
    app.allow_writes = 1;
    TCHECK(zk_data_start(&app) == 0, "start rw");
    for (i = 0; i < 100; i++) {
        zk_data_get(&snap);
        stub_counts(&s, &c);
        if (snap.have_status && c.n_status >= 1 && c.n_stations >= 1) {
            break;
        }
        usleep(20000);
    }
    usleep(200000);
    stub_counts(&s, &c);
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    TCHECK(wait_done(&res, 2000) == 0, "rw stop");
    TEQ_I(res.code, ZK_OK);
    TEQ_I(res.http_status, 200);
    for (i = 0; i < 50; i++) {
        stub_counts(&s, &c1);
        if (c1.n_status > c.n_status) {
            break;
        }
        usleep(20000);
    }
    stub_counts(&s, &c1);
    TEQ_I(c1.n_cancel, 1);
    TEQ_I(c1.n_post, 1);
    TCHECK(strncmp(c1.last_post_req, "POST /api/run/cancel", 20) == 0, "cancel req %s", c1.last_post_req);
    TCHECK(c1.n_status > c.n_status, "no immediate status repoll %d -> %d", c.n_status, c1.n_status);

    zk_data_submit_action(ZK_ACT_PAUSE, "{\"days\":2}");
    TCHECK(wait_done(&res, 2000) == 0, "pause");
    TEQ_I(res.code, ZK_OK);
    stub_counts(&s, &c1);
    TEQ_I(c1.n_pause, 1);
    TEQ_S(c1.last_post_body, "{\"days\":2}");
    TCHECK(strncmp(c1.last_post_req, "POST /api/pause ", 16) == 0, "pause req %s", c1.last_post_req);

    zk_data_submit_action(ZK_ACT_RESUME, NULL);
    TCHECK(wait_done(&res, 2000) == 0, "resume");
    TEQ_I(res.code, ZK_OK);
    stub_counts(&s, &c1);
    TEQ_I(c1.n_resume, 1);
    TEQ_I(c1.n_cancel, 1);
    TCHECK(strncmp(c1.last_post_req, "POST /api/pause/resume", 22) == 0, "resume req %s", c1.last_post_req);
    zk_data_stop();
    stub_stop(&s);
}

static void test_get_does_not_block(void)
{
    stub_t s;
    zk_app_t app;
    zk_snapshot_t snap;
    counts_t c;
    char base[64];
    int i;
    int64_t t0;
    int64_t dt;
    int64_t worst = 0;

    TCHECK(setenv("ZK_POLL_MS_STATUS", "5000", 1) == 0, "setenv");
    TCHECK(stub_start(&s, 1000) == 0, "stub");
    snprintf(base, sizeof base, "http://127.0.0.1:%d", s.port);
    memset(&app, 0, sizeof app);
    app.api = base;
    TCHECK(zk_data_start(&app) == 0, "start");
    for (i = 0; i < 200; i++) {
        stub_counts(&s, &c);
        if (c.n_status >= 1) {
            break;
        }
        usleep(5000);
    }
    stub_counts(&s, &c);
    TCHECK(c.n_status >= 1, "worker never requested status");
    t0 = now_ms();
    zk_data_get(&snap);
    dt = now_ms() - t0;
    TCHECK(dt < 50, "get took %ld ms while worker blocked", (long)dt);
    TEQ_I(snap.have_status, 0);
    t0 = now_ms();
    for (i = 0; i < 100; i++) {
        int64_t a = now_ms();
        zk_data_get(&snap);
        dt = now_ms() - a;
        if (dt > worst) {
            worst = dt;
        }
    }
    dt = now_ms() - t0;
    TCHECK(dt < 100, "100 gets took %ld ms", (long)dt);
    TCHECK(worst < 30, "slowest get %ld ms", (long)worst);
    TEQ_I(snap.have_status, 0);
    for (i = 0; i < 400; i++) {
        zk_data_get(&snap);
        if (snap.have_status && snap.have_stations && snap.have_schedules && snap.have_soil) {
            break;
        }
        usleep(20000);
    }
    TEQ_I(snap.have_status, 1);
    TEQ_I(snap.have_stations, 1);
    TEQ_I(snap.have_schedules, 1);
    TEQ_I(snap.have_soil, 1);
    TEQ_S(snap.stations.items[0].id, "front");
    zk_data_stop();
    stub_stop(&s);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    test_initial_and_repoll();
    test_stale_when_stub_stops();
    test_fixture();
    test_actions();
    test_get_does_not_block();
    return zk_test_report("test_data");
}
