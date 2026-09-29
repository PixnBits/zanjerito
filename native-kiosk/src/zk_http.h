#ifndef ZK_HTTP_H
#define ZK_HTTP_H

#include <stddef.h>

#define ZK_HTTP_MAX_BODY (128 * 1024)

int zk_http_get(const char *base_url, const char *path, int timeout_ms,
                char *buf, size_t cap, int *http_status);
int zk_http_post(const char *base_url, const char *path, const char *json_body,
                 int timeout_ms, char *buf, size_t cap, int *http_status);

#endif
