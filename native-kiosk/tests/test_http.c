#include "zk_test.h"
#include "zk_actions.h"
#include "zk_http.h"
#include "zk_model.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int listen_fd;
    int port;
    pthread_t th;
    volatile int stop;
    volatile int n_accept;
    volatile int hang;
    volatile int oversize;
    char last_req[1024];
    char last_body[1024];
    char get_body[256];
    pthread_mutex_t mu;
} stub_t;

static int read_req(int fd, char *buf, size_t cap, size_t *n)
{
    size_t got = 0;
    *n = 0;
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
    *n = got;
    return 0;
}

static void *stub_main(void *arg)
{
    stub_t *s = (stub_t *)arg;
    while (!s->stop) {
        struct timeval tv;
        fd_set rfds;
        int cfd;
        char req[2048];
        size_t n = 0;
        char *sep;
        long clen = 0;
        FD_ZERO(&rfds);
        FD_SET(s->listen_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        if (select(s->listen_fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
            continue;
        }
        cfd = accept(s->listen_fd, NULL, NULL);
        if (cfd < 0) {
            continue;
        }
        s->n_accept++;
        if (s->hang) {
            while (!s->stop) {
                usleep(20000);
            }
            close(cfd);
            break;
        }
        if (read_req(cfd, req, sizeof(req), &n) != 0) {
            close(cfd);
            continue;
        }
        sep = strstr(req, "\r\n\r\n");
        pthread_mutex_lock(&s->mu);
        zk_str_copy(s->last_req, sizeof(s->last_req), req);
        s->last_body[0] = 0;
        if (sep) {
            const char *p = req;
            while (p && *p) {
                if ((p == req || (p > req && p[-1] == '\n')) &&
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
            {
                size_t hdr_end = (size_t)(sep + 4 - req);
                size_t have = n > hdr_end ? n - hdr_end : 0;
                size_t need = clen > 0 ? (size_t)clen : 0;
                char body[1024];
                size_t bl = 0;
                if (have > 0) {
                    if (have > sizeof(body) - 1) {
                        have = sizeof(body) - 1;
                    }
                    memcpy(body, sep + 4, have);
                    bl = have;
                }
                while (bl < need && bl + 1 < sizeof(body)) {
                    ssize_t r = recv(cfd, body + bl, (need - bl) < (sizeof(body) - 1 - bl) ? (need - bl) : (sizeof(body) - 1 - bl), 0);
                    if (r <= 0) {
                        break;
                    }
                    bl += (size_t)r;
                }
                body[bl] = 0;
                zk_str_copy(s->last_body, sizeof(s->last_body), body);
            }
        }
        pthread_mutex_unlock(&s->mu);

        if (s->oversize) {
            const char *hdr = "HTTP/1.0 200 OK\r\nContent-Length: 200000\r\nConnection: close\r\n\r\n";
            (void)send(cfd, hdr, strlen(hdr), MSG_NOSIGNAL);
        } else if (strncmp(req, "GET ", 4) == 0) {
            char hdr[256];
            int m = snprintf(hdr, sizeof(hdr),
                             "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                             strlen(s->get_body));
            (void)send(cfd, hdr, (size_t)m, MSG_NOSIGNAL);
            (void)send(cfd, s->get_body, strlen(s->get_body), MSG_NOSIGNAL);
        } else {
            const char *resp = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
            (void)send(cfd, resp, strlen(resp), MSG_NOSIGNAL);
        }
        close(cfd);
    }
    return NULL;
}

static int stub_start(stub_t *s)
{
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int on = 1;
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->mu, NULL);
    zk_str_copy(s->get_body, sizeof(s->get_body), "{\"ok\":true}");
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) {
        return -1;
    }
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->listen_fd);
        return -1;
    }
    if (listen(s->listen_fd, 8) < 0) {
        close(s->listen_fd);
        return -1;
    }
    if (getsockname(s->listen_fd, (struct sockaddr *)&addr, &alen) < 0) {
        close(s->listen_fd);
        return -1;
    }
    s->port = ntohs(addr.sin_port);
    if (pthread_create(&s->th, NULL, stub_main, s) != 0) {
        close(s->listen_fd);
        return -1;
    }
    return 0;
}

static void stub_stop(stub_t *s)
{
    s->stop = 1;
    shutdown(s->listen_fd, SHUT_RDWR);
    close(s->listen_fd);
    pthread_join(s->th, NULL);
    pthread_mutex_destroy(&s->mu);
}

int main(void)
{
    stub_t s;
    char base[64];
    char buf[4096];
    int status = 0;
    zk_actions_t act;
    int rc;

    TCHECK(stub_start(&s) == 0, "stub start");
    snprintf(base, sizeof(base), "http://127.0.0.1:%d", s.port);

    rc = zk_http_get(base, "/api/status", 2000, buf, sizeof(buf), &status);
    TEQ_I(rc, ZK_OK);
    TEQ_I(status, 200);
    TEQ_S(buf, "{\"ok\":true}");

    memset(&act, 0, sizeof(act));
    zk_str_copy(act.base, sizeof(act.base), base);
    act.allow_writes = 0;
    s.n_accept = 0;
    rc = zk_action_stop(&act, &status);
    TEQ_I(rc, ZK_RO);
    usleep(80000);
    TEQ_I(s.n_accept, 0);

    act.allow_writes = 1;
    rc = zk_action_stop(&act, &status);
    TEQ_I(rc, ZK_OK);
    TEQ_I(status, 200);
    pthread_mutex_lock(&s.mu);
    TCHECK(strncmp(s.last_req, "POST /api/run/cancel", 20) == 0, "stop path %s", s.last_req);
    pthread_mutex_unlock(&s.mu);

    rc = zk_action_pause(&act, "{\"days\":2,\"reason\":\"kiosk\"}", &status);
    TEQ_I(rc, ZK_OK);
    pthread_mutex_lock(&s.mu);
    TCHECK(strncmp(s.last_req, "POST /api/pause", 15) == 0, "pause path");
    TEQ_S(s.last_body, "{\"days\":2,\"reason\":\"kiosk\"}");
    pthread_mutex_unlock(&s.mu);

    rc = zk_action_resume(&act, &status);
    TEQ_I(rc, ZK_OK);
    pthread_mutex_lock(&s.mu);
    TCHECK(strncmp(s.last_req, "POST /api/pause/resume", 22) == 0, "resume path");
    pthread_mutex_unlock(&s.mu);

    /* localhost alias */
    snprintf(base, sizeof(base), "http://localhost:%d", s.port);
    rc = zk_http_get(base, "/api/status", 2000, buf, sizeof(buf), &status);
    TEQ_I(rc, ZK_OK);

    stub_stop(&s);

    /* hang / timeout */
    TCHECK(stub_start(&s) == 0, "stub2");
    s.hang = 1;
    snprintf(base, sizeof(base), "http://127.0.0.1:%d", s.port);
    rc = zk_http_get(base, "/slow", 200, buf, sizeof(buf), &status);
    TEQ_I(rc, ZK_ERR_TIMEOUT);
    stub_stop(&s);

    /* oversize Content-Length */
    TCHECK(stub_start(&s) == 0, "stub3");
    s.oversize = 1;
    snprintf(base, sizeof(base), "http://127.0.0.1:%d", s.port);
    rc = zk_http_get(base, "/big", 2000, buf, sizeof(buf), &status);
    TEQ_I(rc, ZK_ERR_OVERFLOW);
    stub_stop(&s);

    rc = zk_http_get("https://127.0.0.1", "/x", 100, buf, sizeof(buf), &status);
    TEQ_I(rc, ZK_ERR_ADDR);

    return zk_test_report("test_http");
}
