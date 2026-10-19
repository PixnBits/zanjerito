#include "zk_text.h"

#include <stdint.h>
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

/* Byte length of one UTF-8 sequence. 0 at NUL or when avail is 0.
 * -1 if the next byte is not a sequence we keep (invalid, truncated, overlong).
 * Newlines and other controls are valid sequences; the display fold drops them. */
static int utf8_take(const unsigned char *s, size_t avail, uint32_t *cp)
{
    unsigned char c;
    unsigned char c1;
    if (!s || avail == 0 || s[0] == 0) {
        return 0;
    }
    c = s[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c < 0xC2 || c > 0xF4) {
        return -1;
    }
    if (c <= 0xDF) {
        if (avail < 2 || (s[1] & 0xC0) != 0x80) {
            return -1;
        }
        *cp = ((uint32_t)(c & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
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
        *cp = ((uint32_t)(c & 0x0Fu) << 12) | ((uint32_t)(c1 & 0x3Fu) << 6) | (uint32_t)(s[2] & 0x3Fu);
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
    *cp = ((uint32_t)(c & 0x07u) << 18) | ((uint32_t)(c1 & 0x3Fu) << 12) | ((uint32_t)(s[2] & 0x3Fu) << 6) |
          (uint32_t)(s[3] & 0x3Fu);
    return 4;
}

int zk_utf8_ncopy(char *dst, size_t cap, const char *src, size_t src_n)
{
    size_t i = 0;
    size_t o = 0;
    if (!dst || cap == 0) {
        return -1;
    }
    if (!src) {
        dst[0] = 0;
        return 0;
    }
    while (i < src_n && src[i]) {
        uint32_t cp = 0;
        int sl = utf8_take((const unsigned char *)src + i, src_n - i, &cp);
        if (sl < 0) {
            i++;
            continue;
        }
        if (sl == 0) {
            break;
        }
        if (o + (size_t)sl + 1 > cap) {
            break;
        }
        memcpy(dst + o, src + i, (size_t)sl);
        o += (size_t)sl;
        i += (size_t)sl;
    }
    dst[o] = 0;
    return 0;
}

void zk_utf8_seal(char *s)
{
    size_t i = 0;
    size_t n;
    if (!s) {
        return;
    }
    n = strlen(s);
    while (i < n) {
        uint32_t cp = 0;
        int sl = utf8_take((const unsigned char *)s + i, n - i, &cp);
        if (sl <= 0) {
            s[i] = 0;
            return;
        }
        i += (size_t)sl;
    }
}

/* Latin Extended bases. "." has no ASCII letter and is dropped. */
static const char k_ext_a[] =
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL"
    "lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
static const char k_ext_b[] =
    "bBBbTtOCcADDdtRSOFfGGhIIKkllTNnOOoOoPpyTtErtTtTUuUVYyZzEEeetTtiw"
    "dlarDDdLLlNNnAaIiOoUuUuUuUuUutAaAaAaGgGgKkOoOoEejDDdGgHWNnAaAaOo"
    "AaAaEeEeIiIiOoOoRrRrUuUuSsTtYyHhNdOoZzAaEeOoOoOoOoYylntidqACcLTs"
    "zGgBUTEeJjSqRrYy";
static const char k_ext_add[] =
    "AaBbBbBbCcDdDdDdDdDdEeEeEeEeEeFfGgHhHhHhHhHhIiIiKkKkKkLlLlLlLlMm"
    "MmMmNnNnNnNnOoOoOoOoPpPpRrRrRrRrSsSsSsSsSsTtTtTtTtUuUuUuUuUuVvVv"
    "WwWwWwWwWwXxXxYyZzZzZzhtwyasllSdAaAaAaAaAaAaAaAaAaAaAaAaEeEeEeEe"
    "EeEeEeEeIiIiOoOoOoOoOoOoOoOoOoOoOoOoUuUuUuUuUuUuUuYyYyYyYy....Yy";

/* base, mark, precomposed. Latin-1 only; ASCII bases. */
static const uint16_t k_comp[] = {
    0x41, 0x300, 0xC0, 0x41, 0x301, 0xC1, 0x41, 0x302, 0xC2, 0x41, 0x303, 0xC3, 0x41, 0x308, 0xC4, 0x41, 0x30A, 0xC5,
    0x61, 0x300, 0xE0, 0x61, 0x301, 0xE1, 0x61, 0x302, 0xE2, 0x61, 0x303, 0xE3, 0x61, 0x308, 0xE4, 0x61, 0x30A, 0xE5,
    0x43, 0x327, 0xC7, 0x63, 0x327, 0xE7, 0x45, 0x300, 0xC8, 0x45, 0x301, 0xC9, 0x45, 0x302, 0xCA, 0x45, 0x308, 0xCB,
    0x65, 0x300, 0xE8, 0x65, 0x301, 0xE9, 0x65, 0x302, 0xEA, 0x65, 0x308, 0xEB, 0x49, 0x300, 0xCC, 0x49, 0x301, 0xCD,
    0x49, 0x302, 0xCE, 0x49, 0x308, 0xCF, 0x69, 0x300, 0xEC, 0x69, 0x301, 0xED, 0x69, 0x302, 0xEE, 0x69, 0x308, 0xEF,
    0x4E, 0x303, 0xD1, 0x6E, 0x303, 0xF1, 0x4F, 0x300, 0xD2, 0x4F, 0x301, 0xD3, 0x4F, 0x302, 0xD4, 0x4F, 0x303, 0xD5,
    0x4F, 0x308, 0xD6, 0x6F, 0x300, 0xF2, 0x6F, 0x301, 0xF3, 0x6F, 0x302, 0xF4, 0x6F, 0x303, 0xF5, 0x6F, 0x308, 0xF6,
    0x55, 0x300, 0xD9, 0x55, 0x301, 0xDA, 0x55, 0x302, 0xDB, 0x55, 0x308, 0xDC, 0x75, 0x300, 0xF9, 0x75, 0x301, 0xFA,
    0x75, 0x302, 0xFB, 0x75, 0x308, 0xFC, 0x59, 0x301, 0xDD, 0x79, 0x301, 0xFD, 0x79, 0x308, 0xFF,
};

_Static_assert(sizeof k_ext_a - 1 == 128, "ext A");
_Static_assert(sizeof k_ext_b - 1 == 208, "ext B");
_Static_assert(sizeof k_ext_add - 1 == 256, "ext add");
_Static_assert(sizeof k_comp / sizeof k_comp[0] == 53 * 3, "compose");

static int is_combining(uint32_t cp)
{
    if (cp >= 0x0300 && cp <= 0x036F) {
        return 1;
    }
    if (cp >= 0x1AB0 && cp <= 0x1AFF) {
        return 1;
    }
    if (cp >= 0x1DC0 && cp <= 0x1DFF) {
        return 1;
    }
    if (cp >= 0x20D0 && cp <= 0x20FF) {
        return 1;
    }
    if (cp >= 0xFE20 && cp <= 0xFE2F) {
        return 1;
    }
    return 0;
}

static int compose_latin1(uint32_t base, uint32_t mark, uint32_t *out)
{
    size_t i;
    for (i = 0; i + 2 < sizeof k_comp / sizeof k_comp[0]; i += 3) {
        if (k_comp[i] == base && k_comp[i + 1] == mark) {
            *out = k_comp[i + 2];
            return 1;
        }
    }
    return 0;
}

static char latin_base(uint32_t cp)
{
    if (cp >= 0x0100 && cp <= 0x017F) {
        return k_ext_a[cp - 0x0100];
    }
    if (cp >= 0x0180 && cp <= 0x024F) {
        return k_ext_b[cp - 0x0180];
    }
    if (cp >= 0x1E00 && cp <= 0x1EFF) {
        return k_ext_add[cp - 0x1E00];
    }
    return '.';
}

static int in_subset(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7E) {
        return 1;
    }
    if (cp == 0x00A0) {
        return 1;
    }
    if (cp >= 0x00A1 && cp <= 0x00FF && cp != 0x00AD && cp != 0x00D7) {
        return 1;
    }
    if (cp == 0x2013 || cp == 0x2019 || cp == 0x2026 || cp == 0x203A) {
        return 1;
    }
    return 0;
}

/* 0 drop, 1 emit *out, 2 ASCII space (collapsed by the caller). */
static int fold_cp(uint32_t cp, uint32_t *out)
{
    char b;
    if (cp == '\t' || cp == '\n' || cp == '\r') {
        return 2;
    }
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) {
        return 0;
    }
    if (cp == 0x00AD || cp == 0x2060 || cp == 0xFEFF) {
        return 0;
    }
    if (cp >= 0x200B && cp <= 0x200D) {
        return 0;
    }
    if (cp >= 0xFE00 && cp <= 0xFE0F) {
        return 0;
    }
    if (cp == 0x2000 || cp == 0x2001) {
        return 0;
    }
    /* U+0020 collapses and trims. U+00A0 stays; copy relies on NBSP. */
    if (cp == ' ' || (cp >= 0x2002 && cp <= 0x200A) || cp == 0x202F || cp == 0x3000) {
        return 2;
    }
    if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2014 || cp == 0x2015 || cp == 0x2212) {
        *out = 0x2013;
        return 1;
    }
    if (cp == 0x2018 || cp == 0x201B || cp == 0x2032) {
        *out = 0x2019;
        return 1;
    }
    if (cp == 0x201C || cp == 0x201D || cp == 0x201E) {
        *out = 0x22;
        return 1;
    }
    if (cp == 0x2022 || cp == 0x2219) {
        *out = 0x00B7;
        return 1;
    }
    if (cp == 0x00D7) {
        *out = 'x';
        return 1;
    }
    if (in_subset(cp)) {
        *out = cp;
        return 1;
    }
    b = latin_base(cp);
    if (b != '.') {
        *out = (uint32_t)(unsigned char)b;
        return 1;
    }
    return 0;
}

static int emit_utf8(char *dst, size_t cap, size_t *used, uint32_t cp)
{
    unsigned char buf[4];
    int n;
    if (cp < 0x80) {
        buf[0] = (unsigned char)cp;
        n = 1;
    } else if (cp < 0x800) {
        buf[0] = (unsigned char)(0xC0 | (cp >> 6));
        buf[1] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        buf[0] = (unsigned char)(0xE0 | (cp >> 12));
        buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 3;
    } else if (cp <= 0x10FFFF) {
        buf[0] = (unsigned char)(0xF0 | (cp >> 18));
        buf[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 4;
    } else {
        return 0;
    }
    if (*used + (size_t)n + 1 > cap) {
        return -1;
    }
    memcpy(dst + *used, buf, (size_t)n);
    *used += (size_t)n;
    dst[*used] = 0;
    return 0;
}

int zk_text_display(char *dst, size_t cap, const char *src)
{
    enum { CP_MAX = 1024, OUT_MAX = 4096, SRC_MAX = 16384 };
    uint32_t cps[CP_MAX];
    char out[OUT_MAX];
    size_t i = 0;
    size_t used = 0;
    int n = 0;
    int k;
    int started = 0;
    int pending_sp = 0;
    if (!dst || cap == 0) {
        return -1;
    }
    dst[0] = 0;
    if (!src) {
        return 0;
    }
    while (src[i] && i < SRC_MAX && n < CP_MAX) {
        uint32_t cp = 0;
        size_t avail = 0;
        int sl;
        while (avail < 4 && i + avail < SRC_MAX && src[i + avail]) {
            avail++;
        }
        sl = utf8_take((const unsigned char *)src + i, avail, &cp);
        if (sl < 0) {
            i++;
            continue;
        }
        if (sl == 0) {
            break;
        }
        if (is_combining(cp)) {
            uint32_t pre = 0;
            if (n > 0 && compose_latin1(cps[n - 1], cp, &pre)) {
                cps[n - 1] = pre;
            }
        } else {
            cps[n++] = cp;
        }
        i += (size_t)sl;
    }
    out[0] = 0;
    for (k = 0; k < n; k++) {
        uint32_t one = 0;
        int kind = fold_cp(cps[k], &one);
        if (kind == 0) {
            continue;
        }
        if (kind == 2) {
            if (started) {
                pending_sp = 1;
            }
            continue;
        }
        if (pending_sp) {
            if (emit_utf8(out, sizeof out, &used, ' ') != 0) {
                break;
            }
            pending_sp = 0;
        }
        if (emit_utf8(out, sizeof out, &used, one) != 0) {
            break;
        }
        started = 1;
    }
    return zk_utf8_ncopy(dst, cap, out, used);
}
