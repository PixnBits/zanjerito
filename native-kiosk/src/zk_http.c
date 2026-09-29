#include "zk_http.h"

#include "zk_model.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HDR_MAX 8192
#define HOST_MAX 128

static int64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int poll_fd(int fd, short events, int64_t deadline_ms)
{
    for (;;) {
        int64_t n = now_ms();
        int to;
        struct pollfd p;
        int r;
        if (n >= deadline_ms) {
            return ZK_ERR_TIMEOUT;
        }
        to = (int)(deadline_ms - n);
        p.fd = fd;
        p.events = events;
        p.revents = 0;
        r = poll(&p, 1, to);
        if (r < 0 && errno == EINTR) {
            continue;
        }
        if (r == 0) {
            return ZK_ERR_TIMEOUT;
        }
        if (r < 0) {
            return ZK_ERR_IO;
        }
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (!(p.revents & events)) {
                return ZK_ERR_IO;
            }
        }
        return ZK_OK;
    }
}

static int parse_base(const char *base, char *host, size_t host_cap, int *port)
{
    const char *p;
    const char *h;
    size_t n;
    if (!base || !host || host_cap == 0 || !port) {
        return ZK_ERR_ARG;
    }
    host[0] = 0;
    *port = 80;
    if (strncmp(base, "http://", 7) != 0) {
        return ZK_ERR_ADDR;
    }
    p = base + 7;
    if (*p == '[') {
        return ZK_ERR_ADDR;
    }
    h = p;
    while (*p && *p != ':' && *p != '/' && *p != ' ') {
        p++;
    }
    n = (size_t)(p - h);
    if (n == 0 || n + 1 > host_cap) {
        return ZK_ERR_ADDR;
    }
    memcpy(host, h, n);
    host[n] = 0;
    if (*p == ':') {
        int prt = 0;
        p++;
        if (*p < '0' || *p > '9') {
            return ZK_ERR_ADDR;
        }
        while (*p >= '0' && *p <= '9') {
            prt = prt * 10 + (*p - '0');
            if (prt > 65535) {
                return ZK_ERR_ADDR;
            }
            p++;
        }
        if (prt <= 0) {
            return ZK_ERR_ADDR;
        }
        *port = prt;
    }
    if (strcmp(host, "localhost") == 0) {
        zk_str_copy(host, host_cap, "127.0.0.1");
    }
    return ZK_OK;
}

static int connect_host(const char *host, int port, int64_t deadline_ms, int *out_fd)
{
    struct sockaddr_in addr;
    int fd, flags, r;
    int err;
    socklen_t elen;
    struct in_addr in;
    *out_fd = -1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &in) == 1) {
        addr.sin_addr = in;
    } else {
        struct addrinfo hints;
        struct addrinfo *res = NULL;
        int ga;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        ga = getaddrinfo(host, NULL, &hints, &res);
        if (ga != 0 || !res) {
            if (res) {
                freeaddrinfo(res);
            }
            return ZK_ERR_ADDR;
        }
        memcpy(&addr, res->ai_addr, sizeof(addr));
        addr.sin_port = htons((uint16_t)port);
        freeaddrinfo(res);
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return ZK_ERR_IO;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return ZK_ERR_IO;
    }
    r = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (r < 0 && errno != EINPROGRESS) {
        close(fd);
        return ZK_ERR_IO;
    }
    if (r < 0) {
        int pr = poll_fd(fd, POLLOUT, deadline_ms);
        if (pr != ZK_OK) {
            close(fd);
            return pr;
        }
        err = 0;
        elen = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err != 0) {
            close(fd);
            return ZK_ERR_IO;
        }
    }
    *out_fd = fd;
    return ZK_OK;
}

static int send_all(int fd, const char *p, size_t n, int64_t deadline_ms)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w;
        int pr = poll_fd(fd, POLLOUT, deadline_ms);
        if (pr != ZK_OK) {
            return pr;
        }
        w = send(fd, p + off, n - off, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return ZK_ERR_IO;
        }
        if (w == 0) {
            return ZK_ERR_IO;
        }
        off += (size_t)w;
    }
    return ZK_OK;
}

static int recvn(int fd, char *p, size_t n, int64_t deadline_ms, size_t *got)
{
    *got = 0;
    while (*got < n) {
        ssize_t r;
        int pr = poll_fd(fd, POLLIN, deadline_ms);
        if (pr != ZK_OK) {
            return pr;
        }
        r = recv(fd, p + *got, n - *got, 0);
        if (r < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return ZK_ERR_IO;
        }
        if (r == 0) {
            return ZK_OK; /* EOF */
        }
        *got += (size_t)r;
    }
    return ZK_OK;
}

static const char *find_hdr(const char *hdrs, const char *name)
{
    size_t nlen = strlen(name);
    const char *p = hdrs;
    while (*p) {
        const char *nl;
        size_t i;
        int match = 1;
        for (i = 0; i < nlen; i++) {
            char a = p[i];
            char b = name[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                match = 0;
                break;
            }
        }
        if (match && p[nlen] == ':') {
            p += nlen + 1;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            return p;
        }
        nl = strstr(p, "\r\n");
        if (!nl) {
            break;
        }
        p = nl + 2;
    }
    return NULL;
}

static int http_exchange(const char *base_url, const char *path, const char *method,
                         const char *json_body, int timeout_ms,
                         char *buf, size_t cap, int *http_status)
{
    char host[HOST_MAX];
    char req[HDR_MAX];
    char hdr[HDR_MAX];
    int port, fd, rc, status;
    int64_t deadline;
    size_t body_len, req_len, hdr_len, i;
    const char *cl;
    int has_cl = 0;
    long content_len = -1;
    size_t body_got = 0;
    size_t limit;
    char *sep;

    if (http_status) {
        *http_status = 0;
    }
    if (!base_url || !path || !method) {
        return ZK_ERR_ARG;
    }
    if (!buf || cap == 0) {
        return ZK_ERR_ARG;
    }
    buf[0] = 0;
    if (timeout_ms <= 0) {
        timeout_ms = 5000;
    }
    rc = parse_base(base_url, host, sizeof(host), &port);
    if (rc != ZK_OK) {
        return rc;
    }
    if (path[0] != '/') {
        return ZK_ERR_ARG;
    }
    body_len = json_body ? strlen(json_body) : 0;
    if (strcmp(method, "POST") == 0) {
        int n = snprintf(req, sizeof(req),
                         "POST %s HTTP/1.0\r\n"
                         "Host: %s:%d\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         path, host, port, body_len);
        if (n < 0 || (size_t)n >= sizeof(req)) {
            return ZK_ERR_OVERFLOW;
        }
        req_len = (size_t)n;
    } else {
        int n = snprintf(req, sizeof(req),
                         "GET %s HTTP/1.0\r\n"
                         "Host: %s:%d\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         path, host, port);
        if (n < 0 || (size_t)n >= sizeof(req)) {
            return ZK_ERR_OVERFLOW;
        }
        req_len = (size_t)n;
    }

    deadline = now_ms() + timeout_ms;
    rc = connect_host(host, port, deadline, &fd);
    if (rc != ZK_OK) {
        return rc;
    }
    rc = send_all(fd, req, req_len, deadline);
    if (rc == ZK_OK && body_len > 0) {
        rc = send_all(fd, json_body, body_len, deadline);
    }
    if (rc != ZK_OK) {
        close(fd);
        return rc;
    }

    hdr_len = 0;
    sep = NULL;
    while (hdr_len + 1 < sizeof(hdr)) {
        size_t got = 0;
        rc = recvn(fd, hdr + hdr_len, 1, deadline, &got);
        if (rc != ZK_OK) {
            close(fd);
            return rc;
        }
        if (got == 0) {
            close(fd);
            return ZK_ERR_PARSE;
        }
        hdr_len += got;
        hdr[hdr_len] = 0;
        sep = strstr(hdr, "\r\n\r\n");
        if (sep) {
            break;
        }
    }
    if (!sep) {
        close(fd);
        return ZK_ERR_PARSE;
    }
    *sep = 0;
    {
        unsigned major = 0, minor = 0;
        if (sscanf(hdr, "HTTP/%u.%u %d", &major, &minor, &status) != 3) {
            close(fd);
            return ZK_ERR_PARSE;
        }
    }
    if (http_status) {
        *http_status = status;
    }
    cl = find_hdr(hdr, "content-length");
    if (cl) {
        content_len = 0;
        has_cl = 1;
        while (*cl >= '0' && *cl <= '9') {
            content_len = content_len * 10 + (*cl - '0');
            if (content_len > ZK_HTTP_MAX_BODY + 1) {
                break;
            }
            cl++;
        }
    }
    limit = cap < (size_t)ZK_HTTP_MAX_BODY ? cap : (size_t)ZK_HTTP_MAX_BODY;
    if (has_cl && content_len > (long)ZK_HTTP_MAX_BODY) {
        close(fd);
        return ZK_ERR_OVERFLOW;
    }
    if (has_cl && (size_t)content_len > cap) {
        close(fd);
        return ZK_ERR_OVERFLOW;
    }

    /* Any bytes already read past headers (should be none: we read bytewise). */
    i = (size_t)((sep + 4) - hdr);
    if (i < hdr_len) {
        size_t extra = hdr_len - i;
        if (extra > limit) {
            close(fd);
            return ZK_ERR_OVERFLOW;
        }
        memcpy(buf, hdr + i, extra);
        body_got = extra;
    }

    if (has_cl) {
        size_t need = (size_t)content_len;
        while (body_got < need) {
            size_t got = 0;
            size_t chunk = need - body_got;
            if (body_got >= limit) {
                close(fd);
                return ZK_ERR_OVERFLOW;
            }
            if (chunk > limit - body_got) {
                chunk = limit - body_got;
            }
            rc = recvn(fd, buf + body_got, chunk, deadline, &got);
            if (rc != ZK_OK) {
                close(fd);
                return rc;
            }
            if (got == 0) {
                break;
            }
            body_got += got;
        }
        if (body_got != need) {
            close(fd);
            return ZK_ERR_PARSE;
        }
    } else {
        for (;;) {
            size_t got = 0;
            size_t room;
            if (body_got >= limit) {
                /* peek one more byte to detect overflow */
                char dump;
                size_t g2 = 0;
                rc = recvn(fd, &dump, 1, deadline, &g2);
                close(fd);
                if (rc == ZK_OK && g2 > 0) {
                    return ZK_ERR_OVERFLOW;
                }
                if (body_got > (size_t)ZK_HTTP_MAX_BODY) {
                    return ZK_ERR_OVERFLOW;
                }
                break;
            }
            room = limit - body_got;
            rc = recvn(fd, buf + body_got, room, deadline, &got);
            if (rc == ZK_ERR_TIMEOUT && body_got > 0) {
                /* HTTP/1.0 may just stall; treat timeout with no Content-Length as end? No. */
                close(fd);
                return rc;
            }
            if (rc != ZK_OK) {
                close(fd);
                return rc;
            }
            if (got == 0) {
                break;
            }
            body_got += got;
        }
    }
    close(fd);
    if (body_got < cap) {
        buf[body_got] = 0;
    } else if (cap > 0) {
        buf[cap - 1] = 0;
    }
    return ZK_OK;
}

int zk_http_get(const char *base_url, const char *path, int timeout_ms,
                char *buf, size_t cap, int *http_status)
{
    return http_exchange(base_url, path, "GET", NULL, timeout_ms, buf, cap, http_status);
}

int zk_http_post(const char *base_url, const char *path, const char *json_body,
                 int timeout_ms, char *buf, size_t cap, int *http_status)
{
    return http_exchange(base_url, path, "POST", json_body, timeout_ms, buf, cap, http_status);
}
