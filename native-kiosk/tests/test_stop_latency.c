/* STOP must be posted while a poll GET is still in flight. Offline only. */
#include "zk_test.h"
#include "data.h"

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HANDLERS_MAX 32

typedef struct {
    int n_status;
    int n_get;
    int n_post;
    int n_cancel;
    int n_pause;
    int n_resume;
    int get_busy;
    int64_t last_cancel_us;
    char last_pause[256];
} counts_t;

typedef struct {
    int listen_fd;
    int port;
    pthread_t accept_th;
    int accept_started;
    pthread_t handlers[HANDLERS_MAX];
    int n_handlers;
    int stop;
    int get_delay_ms;
    int post_delay_ms;
    int mu_live;
    pthread_mutex_t mu;
    counts_t counts;
} stub_t;

typedef struct {
    stub_t *s;
    int fd;
} hctx_t;

static long g_lat_i = -1;
static long g_lat_ii = -1;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void sleep_ms(int ms)
{
    int64_t t0 = now_ms();
    while (now_ms() - t0 < ms) {
        usleep(20000);
    }
}

static void on_alarm(int sig)
{
    const char msg[] = "test_stop_latency: timeout\n";
    ssize_t n;
    (void)sig;
    n = write(STDERR_FILENO, msg, sizeof msg - 1);
    (void)n;
    _exit(1);
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

static void sleep_flag(stub_t *s, int ms)
{
    int left = ms;
    while (left > 0 && !__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
        int slice = left > 50 ? 50 : left;
        usleep((useconds_t)slice * 1000);
        left -= slice;
    }
}

static int read_req(stub_t *s, int fd, char *buf, size_t cap, char *body, size_t body_cap)
{
    size_t got = 0;
    char *sep;
    long clen = 0;
    buf[0] = 0;
    if (body && body_cap) {
        body[0] = 0;
    }
    while (got + 1 < cap && !__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
        ssize_t r = recv(fd, buf + got, cap - 1 - got, 0);
        if (r < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
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
    if (!strstr(buf, "\r\n\r\n")) {
        return -1;
    }
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
        while (bl < need && bl + 1 < body_cap && !__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
            size_t room = body_cap - 1 - bl;
            size_t chunk = need - bl < room ? need - bl : room;
            ssize_t r = recv(fd, body + bl, chunk, 0);
            if (r < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                break;
            }
            if (r == 0) {
                break;
            }
            bl += (size_t)r;
        }
        body[bl] = 0;
    }
    return 0;
}

static void *handler_main(void *arg)
{
    hctx_t *ctx = (hctx_t *)arg;
    stub_t *s = ctx->s;
    int fd = ctx->fd;
    char req[2048];
    char body[512];
    int is_get = 0;
    int delay;
    const char *json = "{}";

    free(ctx);
    if (read_req(s, fd, req, sizeof req, body, sizeof body) != 0) {
        close(fd);
        return NULL;
    }
    pthread_mutex_lock(&s->mu);
    if (strncmp(req, "GET ", 4) == 0) {
        is_get = 1;
        s->counts.n_get++;
        s->counts.get_busy++;
        if (req_is(req, "GET", "/api/kiosk")) {
            s->counts.n_status++;
            json = "{\"now\":\"2026-09-29T06:52:00-06:00\",\"timezone\":\"America/Denver\",\"phase\":\"Idle\","
                   "\"lockout\":false,\"stations\":[],\"soil\":{},\"run\":null}";
        } else if (req_is(req, "GET", "/api/status")) {
            s->counts.n_status++;
            json = "{\"phase\":\"Idle\"}";
        }
    } else if (req_is(req, "POST", "/api/run/cancel")) {
        s->counts.n_post++;
        s->counts.n_cancel++;
        s->counts.last_cancel_us = now_us();
    } else if (req_is(req, "POST", "/api/pause/resume")) {
        s->counts.n_post++;
        s->counts.n_resume++;
    } else if (req_is(req, "POST", "/api/pause")) {
        s->counts.n_post++;
        s->counts.n_pause++;
        zk_str_copy(s->counts.last_pause, sizeof s->counts.last_pause, body);
    } else if (strncmp(req, "POST ", 5) == 0) {
        s->counts.n_post++;
    }
    pthread_mutex_unlock(&s->mu);

    /* Counted above, before the response delay, so a torn-down handler still counts. */
    if (is_get) {
        delay = __atomic_load_n(&s->get_delay_ms, __ATOMIC_ACQUIRE);
    } else {
        delay = __atomic_load_n(&s->post_delay_ms, __ATOMIC_ACQUIRE);
    }
    if (delay > 0) {
        sleep_flag(s, delay);
    }
    if (is_get) {
        pthread_mutex_lock(&s->mu);
        if (s->counts.get_busy > 0) {
            s->counts.get_busy--;
        }
        pthread_mutex_unlock(&s->mu);
    }
    if (!__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
        reply_json(fd, json);
    }
    close(fd);
    return NULL;
}

static void *accept_main(void *arg)
{
    stub_t *s = (stub_t *)arg;
    while (!__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
        fd_set rfds;
        struct timeval tv;
        int cfd;
        hctx_t *ctx;
        struct timeval rcv;
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
        if (s->n_handlers >= HANDLERS_MAX) {
            close(cfd);
            continue;
        }
        rcv.tv_sec = 0;
        rcv.tv_usec = 200000;
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof rcv);
        ctx = (hctx_t *)malloc(sizeof *ctx);
        if (!ctx) {
            close(cfd);
            continue;
        }
        ctx->s = s;
        ctx->fd = cfd;
        if (pthread_create(&s->handlers[s->n_handlers], NULL, handler_main, ctx) != 0) {
            free(ctx);
            close(cfd);
            continue;
        }
        s->n_handlers++;
    }
    return NULL;
}

static void stub_stop(stub_t *s)
{
    int i;
    int n;
    pthread_t hs[HANDLERS_MAX];

    if (!s->mu_live && s->listen_fd < 0 && !s->accept_started) {
        return;
    }
    __atomic_store_n(&s->stop, 1, __ATOMIC_RELEASE);
    if (s->listen_fd >= 0) {
        shutdown(s->listen_fd, SHUT_RDWR);
    }
    if (s->accept_started) {
        pthread_join(s->accept_th, NULL);
        s->accept_started = 0;
    }
    if (s->listen_fd >= 0) {
        close(s->listen_fd);
        s->listen_fd = -1;
    }
    n = s->n_handlers;
    if (n > HANDLERS_MAX) {
        n = HANDLERS_MAX;
    }
    for (i = 0; i < n; i++) {
        hs[i] = s->handlers[i];
    }
    s->n_handlers = 0;
    for (i = 0; i < n; i++) {
        pthread_join(hs[i], NULL);
    }
    if (s->mu_live) {
        pthread_mutex_destroy(&s->mu);
        s->mu_live = 0;
    }
}

static int stub_start(stub_t *s, int get_delay_ms, int post_delay_ms)
{
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    int on = 1;

    memset(s, 0, sizeof *s);
    s->listen_fd = -1;
    if (pthread_mutex_init(&s->mu, NULL) != 0) {
        return -1;
    }
    s->mu_live = 1;
    __atomic_store_n(&s->get_delay_ms, get_delay_ms, __ATOMIC_RELEASE);
    __atomic_store_n(&s->post_delay_ms, post_delay_ms, __ATOMIC_RELEASE);
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) {
        stub_stop(s);
        return -1;
    }
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        listen(s->listen_fd, 64) < 0 ||
        getsockname(s->listen_fd, (struct sockaddr *)&addr, &alen) < 0) {
        stub_stop(s);
        return -1;
    }
    s->port = ntohs(addr.sin_port);
    if (pthread_create(&s->accept_th, NULL, accept_main, s) != 0) {
        stub_stop(s);
        return -1;
    }
    s->accept_started = 1;
    return 0;
}

static void snap(stub_t *s, counts_t *c)
{
    pthread_mutex_lock(&s->mu);
    *c = s->counts;
    pthread_mutex_unlock(&s->mu);
}

static int wait_busy(stub_t *s, int timeout_ms)
{
    int64_t t0 = now_ms();
    for (;;) {
        counts_t c;
        snap(s, &c);
        if (c.get_busy > 0) {
            return 0;
        }
        if (now_ms() - t0 > timeout_ms) {
            return -1;
        }
        usleep(10000);
    }
}

static int wait_n(stub_t *s, int (*field)(const counts_t *), int want, int timeout_ms, counts_t *out)
{
    int64_t t0 = now_ms();
    counts_t c;
    memset(&c, 0, sizeof c);
    for (;;) {
        snap(s, &c);
        if (field(&c) >= want) {
            if (out) {
                *out = c;
            }
            return 0;
        }
        if (now_ms() - t0 > timeout_ms) {
            if (out) {
                *out = c;
            }
            return -1;
        }
        usleep(10000);
    }
}

static int field_cancel(const counts_t *c) { return c->n_cancel; }
static int field_pause(const counts_t *c) { return c->n_pause; }

/* Microseconds from submit to the stub reading the POST, not when this poll notices. */
static long wait_cancel_lat(stub_t *s, int prev, int64_t t0_us, int timeout_ms)
{
    int64_t deadline = now_ms() + timeout_ms;
    for (;;) {
        counts_t c;
        snap(s, &c);
        if (c.n_cancel > prev && c.last_cancel_us > 0) {
            return (long)(c.last_cancel_us - t0_us);
        }
        if (now_ms() > deadline) {
            return -1;
        }
        usleep(2000);
    }
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

static int begin(stub_t *s, int get_ms, int post_ms, int allow, const char *poll_ms)
{
    zk_app_t app;
    char url[64];
    int wr;

    memset(s, 0, sizeof *s);
    s->listen_fd = -1;
    if (poll_ms && poll_ms[0]) {
        if (setenv("ZK_POLL_MS_STATUS", poll_ms, 1) != 0) {
            return -1;
        }
    } else if (unsetenv("ZK_POLL_MS_STATUS") != 0) {
        return -1;
    }
    if (stub_start(s, get_ms, post_ms) != 0) {
        return -1;
    }
    wr = snprintf(url, sizeof url, "http://127.0.0.1:%d", s->port);
    if (wr < 0 || (size_t)wr >= sizeof url) {
        stub_stop(s);
        return -1;
    }
    memset(&app, 0, sizeof app);
    app.api = url;
    app.allow_writes = allow;
    if (zk_data_start(&app) != ZK_OK) {
        zk_data_stop();
        stub_stop(s);
        return -1;
    }
    return 0;
}

static void finish(stub_t *s)
{
    stub_stop(s);
    zk_data_stop();
}

/* (i) 8s GETs. STOP is posted within 1s, not after the poll returns. */
static void test_case_i(void)
{
    stub_t s;
    counts_t c;
    int64_t t0;
    long lat;

    TCHECK(begin(&s, 8000, 0, 1, "2000") == 0, "case i start");
    if (!s.mu_live) {
        return;
    }
    TCHECK(wait_busy(&s, 700) == 0, "case i GET not in flight");
    snap(&s, &c);
    t0 = now_us();
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    lat = wait_cancel_lat(&s, c.n_cancel, t0, 1000);
    g_lat_i = lat;
    TCHECK(lat >= 0 && lat <= 1000000L, "case i POST latency %ld us", lat);
    snap(&s, &c);
    TEQ_I(c.n_cancel, 1);
    finish(&s);
}

/* (ii) Submit while a 3s GET is mid-flight (~4.9s). */
static void test_case_ii(void)
{
    stub_t s;
    counts_t c;
    int64_t t0;
    long lat;

    TCHECK(begin(&s, 3000, 0, 1, "2000") == 0, "case ii start");
    if (!s.mu_live) {
        return;
    }
    sleep_ms(4900);
    TCHECK(wait_busy(&s, 200) == 0, "case ii poll not mid-flight");
    snap(&s, &c);
    t0 = now_us();
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    lat = wait_cancel_lat(&s, c.n_cancel, t0, 1000);
    g_lat_ii = lat;
    TCHECK(lat >= 0 && lat <= 1000000L, "case ii POST latency %ld us", lat);
    finish(&s);
}

/* (iii) Two STOPs 800ms apart during an 8s POST: one POST, result seq is the second. */
static void test_stop_twice_delayed(void)
{
    stub_t s;
    zk_action_result_t r;
    zk_action_result_t after;
    counts_t c;
    uint32_t seq2;

    TCHECK(begin(&s, 0, 8000, 1, "2000") == 0, "delayed coalesce start");
    if (!s.mu_live) {
        return;
    }
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    TCHECK(wait_n(&s, field_cancel, 1, 1000, &c) == 0, "delayed first POST");
    sleep_ms(800);
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    zk_data_action_result(&r);
    seq2 = r.seq;
    TEQ_I(r.pending, 1);
    sleep_ms(150);
    snap(&s, &c);
    TEQ_I(c.n_cancel, 1);
    TEQ_I(c.n_post, 1);
    finish(&s);
    zk_data_action_result(&after);
    TEQ_I(after.done, 1);
    TEQ_I(after.kind, ZK_ACT_STOP);
    TCHECK(after.seq == seq2, "aliased seq %u want %u code %d", after.seq, seq2, after.code);
}

/* Two STOPs with no POST delay, back to back: still one POST. */
static void test_stop_back_to_back(void)
{
    stub_t s;
    zk_action_result_t r;
    counts_t c;
    uint32_t seq2;

    TCHECK(begin(&s, 0, 0, 1, "2000") == 0, "b2b start");
    if (!s.mu_live) {
        return;
    }
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    zk_data_action_result(&r);
    seq2 = r.seq;
    TCHECK(wait_done(&r, 2000) == 0, "b2b done");
    TEQ_I(r.code, ZK_OK);
    TEQ_I(r.kind, ZK_ACT_STOP);
    TCHECK(r.seq == seq2, "b2b seq %u want %u", r.seq, seq2);
    snap(&s, &c);
    TEQ_I(c.n_cancel, 1);
    TEQ_I(c.n_post, 1);
    finish(&s);
}

static void test_resume_back_to_back(void)
{
    stub_t s;
    zk_action_result_t r;
    counts_t c;
    uint32_t seq2;

    TCHECK(begin(&s, 0, 0, 1, "2000") == 0, "resume start");
    if (!s.mu_live) {
        return;
    }
    zk_data_submit_action(ZK_ACT_RESUME, NULL);
    zk_data_submit_action(ZK_ACT_RESUME, NULL);
    zk_data_action_result(&r);
    seq2 = r.seq;
    TCHECK(wait_done(&r, 2000) == 0, "resume done");
    TEQ_I(r.code, ZK_OK);
    TEQ_I(r.kind, ZK_ACT_RESUME);
    TCHECK(r.seq == seq2, "resume seq %u want %u", r.seq, seq2);
    snap(&s, &c);
    TEQ_I(c.n_resume, 1);
    finish(&s);
}

/* Identical pause body while one is in flight: one POST, seq is the later submit. */
static void test_pause_same_body(void)
{
    stub_t s;
    zk_action_result_t r;
    counts_t c;
    uint32_t seq2;
    const char *body = "{\"days\":2,\"reason\":\"kiosk\"}";

    TCHECK(begin(&s, 0, 500, 1, "2000") == 0, "same pause start");
    if (!s.mu_live) {
        return;
    }
    zk_data_submit_action(ZK_ACT_PAUSE, body);
    TCHECK(wait_n(&s, field_pause, 1, 1000, NULL) == 0, "same pause first");
    zk_data_submit_action(ZK_ACT_PAUSE, body);
    zk_data_action_result(&r);
    seq2 = r.seq;
    TCHECK(wait_done(&r, 2000) == 0, "same pause done");
    TEQ_I(r.code, ZK_OK);
    TCHECK(r.seq == seq2, "same pause seq %u want %u", r.seq, seq2);
    snap(&s, &c);
    TEQ_I(c.n_pause, 1);
    TEQ_S(c.last_pause, body);
    finish(&s);
}

/* (iv) A different pause is still sent while STOP is in flight. */
static void test_pause_during_stop(void)
{
    stub_t s;
    counts_t c;
    const char *body = "{\"days\":9,\"reason\":\"kiosk\"}";

    TCHECK(begin(&s, 0, 600, 1, "2000") == 0, "pause during stop start");
    if (!s.mu_live) {
        return;
    }
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    TCHECK(wait_n(&s, field_cancel, 1, 1000, NULL) == 0, "stop in flight");
    zk_data_submit_action(ZK_ACT_PAUSE, body);
    TCHECK(wait_n(&s, field_pause, 1, 2500, &c) == 0, "different pause was dropped");
    TEQ_I(c.n_cancel, 1);
    TEQ_I(c.n_pause, 1);
    TEQ_S(c.last_pause, body);
    finish(&s);
}

static void test_pause_two_bodies(void)
{
    stub_t s;
    counts_t c;

    TCHECK(begin(&s, 0, 400, 1, "2000") == 0, "two pause start");
    if (!s.mu_live) {
        return;
    }
    zk_data_submit_action(ZK_ACT_PAUSE, "{\"days\":1}");
    TCHECK(wait_n(&s, field_pause, 1, 1000, NULL) == 0, "first pause body");
    zk_data_submit_action(ZK_ACT_PAUSE, "{\"days\":2}");
    TCHECK(wait_n(&s, field_pause, 2, 2000, &c) == 0, "second pause body dropped");
    TEQ_I(c.n_pause, 2);
    TEQ_S(c.last_pause, "{\"days\":2}");
    finish(&s);
}

/* One STOP in flight plus 8 distinct pauses fills the queue; the 9th overflows. */
static void test_overflow(void)
{
    stub_t s;
    zk_action_result_t r;
    counts_t c;
    int i;

    TCHECK(begin(&s, 0, 4000, 1, "2000") == 0, "overflow start");
    if (!s.mu_live) {
        return;
    }
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    TCHECK(wait_n(&s, field_cancel, 1, 1000, NULL) == 0, "overflow stop in flight");
    for (i = 0; i < 8; i++) {
        char body[32];
        snprintf(body, sizeof body, "{\"k\":%d}", i);
        zk_data_submit_action(ZK_ACT_PAUSE, body);
        zk_data_action_result(&r);
        TCHECK(r.code != ZK_ERR_OVERFLOW, "pause %d overflowed early", i);
        TEQ_I(r.pending, 1);
        TEQ_I(r.done, 0);
    }
    zk_data_submit_action(ZK_ACT_PAUSE, "{\"k\":8}");
    zk_data_action_result(&r);
    TEQ_I(r.pending, 0);
    TEQ_I(r.done, 1);
    TEQ_I(r.code, ZK_ERR_OVERFLOW);
    snap(&s, &c);
    TEQ_I(c.n_cancel, 1);
    TEQ_I(c.n_pause, 0);
    finish(&s);
}

/* (v) Read-only: ZK_RO and no POST while GETs are slow. */
static void test_readonly(void)
{
    stub_t s;
    zk_action_result_t r;
    counts_t c;
    int64_t t0;
    int64_t dt;

    TCHECK(begin(&s, 8000, 0, 0, "2000") == 0, "ro start");
    if (!s.mu_live) {
        return;
    }
    TCHECK(wait_busy(&s, 700) == 0, "ro GET not in flight");
    t0 = now_ms();
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    TCHECK(wait_done(&r, 1000) == 0, "ro result");
    dt = now_ms() - t0;
    TCHECK(dt < 1000, "ro took %ld ms", (long)dt);
    TEQ_I(r.code, ZK_RO);
    TEQ_I(r.http_status, 0);
    TEQ_I(r.kind, ZK_ACT_STOP);
    snap(&s, &c);
    TEQ_I(c.n_post, 0);
    TEQ_I(c.n_cancel, 0);
    finish(&s);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, on_alarm);
    alarm(45);
    test_case_i();
    test_case_ii();
    test_stop_twice_delayed();
    test_stop_back_to_back();
    test_resume_back_to_back();
    test_pause_same_body();
    test_pause_during_stop();
    test_pause_two_bodies();
    test_overflow();
    test_readonly();
    printf("post_latency_us case_i=%ld case_ii=%ld\n", g_lat_i, g_lat_ii);
    fflush(stdout);
    return zk_test_report("test_stop_latency");
}
