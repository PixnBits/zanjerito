#include "zk_text.h"

#include <string.h>

enum { FIT_SRC_MAX = 2048, FIT_TMP = 1024 };

/* Byte length of one well-formed code point, 0 at NUL, -1 if this byte
 * cannot start a code point we are willing to keep (newline, invalid,
 * truncated, overlong). */
static int utf8_seq(const unsigned char *s, size_t avail)
{
    unsigned char c;
    unsigned char c1;
    if (avail == 0 || s[0] == 0) {
        return 0;
    }
    c = s[0];
    if (c == '\n' || c == '\r') {
        return -1;
    }
    if (c < 0x80) {
        return 1;
    }
    if (c < 0xC2 || c > 0xF4) {
        return -1;
    }
    if (c <= 0xDF) {
        if (avail < 2 || (s[1] & 0xC0) != 0x80) {
            return -1;
        }
        return 2;
    }
    if (c <= 0xEF) {
        if (avail < 3) {
            return -1;
        }
        c1 = s[1];
        if ((c1 & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
            return -1;
        }
        if (c == 0xE0 && c1 < 0xA0) {
            return -1;
        }
        if (c == 0xED && c1 >= 0xA0) {
            return -1;
        }
        return 3;
    }
    if (avail < 4) {
        return -1;
    }
    c1 = s[1];
    if ((c1 & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) {
        return -1;
    }
    if (c == 0xF0 && c1 < 0x90) {
        return -1;
    }
    if (c == 0xF4 && c1 >= 0x90) {
        return -1;
    }
    return 4;
}

static size_t valid_prefix(const char *src, int *clean)
{
    size_t n = 0;
    *clean = 1;
    while (n < FIT_SRC_MAX && src[n] != '\0') {
        size_t avail = 0;
        int sl;
        while (avail < 4 && n + avail < FIT_SRC_MAX && src[n + avail] != '\0') {
            avail++;
        }
        sl = utf8_seq((const unsigned char *)src + n, avail);
        if (sl <= 0) {
            *clean = 0;
            break;
        }
        n += (size_t)sl;
    }
    if (src[n] != '\0') {
        *clean = 0;
    }
    return n;
}

int zk_text_fit(char *dst, size_t cap, const char *src, int max_px,
                int (*width_px)(const char *text, size_t nbytes, void *user), void *user)
{
    char tmp[FIT_TMP];
    size_t src_n;
    size_t i;
    size_t best;
    int clean;
    int best_set;
    if (!dst || cap == 0 || !width_px) {
        if (dst && cap > 0) {
            dst[0] = 0;
        }
        return -1;
    }
    if (!src) {
        src = "";
    }
    src_n = valid_prefix(src, &clean);
    if (clean && src_n + 1 <= cap) {
        int w = width_px(src, src_n, user);
        if (w >= 0 && w <= max_px) {
            memcpy(dst, src, src_n);
            dst[src_n] = 0;
            return 0;
        }
    }
    best = 0;
    best_set = 0;
    i = 0;
    for (;;) {
        size_t pre = i;
        int w;
        int sl;
        if (pre + ZK_TEXT_ELLIPSIS_LEN >= cap || pre + ZK_TEXT_ELLIPSIS_LEN + 1 > sizeof tmp) {
            break;
        }
        memcpy(tmp, src, pre);
        memcpy(tmp + pre, ZK_TEXT_ELLIPSIS, ZK_TEXT_ELLIPSIS_LEN);
        tmp[pre + ZK_TEXT_ELLIPSIS_LEN] = 0;
        w = width_px(tmp, pre + ZK_TEXT_ELLIPSIS_LEN, user);
        if (w >= 0 && w <= max_px && (!best_set || pre >= best)) {
            best = pre;
            best_set = 1;
        }
        if (i >= src_n) {
            break;
        }
        sl = utf8_seq((const unsigned char *)src + i, src_n - i);
        if (sl <= 0) {
            break;
        }
        i += (size_t)sl;
    }
    if (!best_set) {
        dst[0] = 0;
        return 0;
    }
    memcpy(dst, src, best);
    memcpy(dst + best, ZK_TEXT_ELLIPSIS, ZK_TEXT_ELLIPSIS_LEN);
    dst[best + ZK_TEXT_ELLIPSIS_LEN] = 0;
    return 0;
}
