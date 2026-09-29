#include "zk_test.h"
#include "zk_text.h"

static int cp_width(const char *text, size_t nbytes, void *user)
{
    size_t i = 0;
    int w = 0;
    int *bad = user;
    if (!text || text[nbytes] != 0) {
        if (bad) {
            *bad = 1;
        }
        return -1;
    }
    while (i < nbytes && text[i]) {
        unsigned char c = (unsigned char)text[i];
        int sl = 1;
        if ((c & 0xE0) == 0xC0) {
            sl = 2;
        } else if ((c & 0xF0) == 0xE0) {
            sl = 3;
        } else if ((c & 0xF8) == 0xF0) {
            sl = 4;
        }
        if (i + (size_t)sl > nbytes) {
            if (bad) {
                *bad = 1;
            }
            return -1;
        }
        w += 10;
        i += (size_t)sl;
    }
    return w;
}

/* Ellipsis adds no width, but only when it is the suffix of this buffer.
 * A prefix measured alone is huge, so width(prefix)+width(ellipsis) cannot
 * see a candidate that fits. */
static int suffix_ellipsis_width(const char *text, size_t nbytes, void *user)
{
    (void)user;
    if (!text || text[nbytes] != 0) {
        return -1;
    }
    if (nbytes >= ZK_TEXT_ELLIPSIS_LEN &&
        memcmp(text + nbytes - ZK_TEXT_ELLIPSIS_LEN, ZK_TEXT_ELLIPSIS, ZK_TEXT_ELLIPSIS_LEN) == 0) {
        return (int)(nbytes - ZK_TEXT_ELLIPSIS_LEN);
    }
    return 1000 + (int)nbytes;
}

static int utf8_ok(const char *s)
{
    size_t i = 0;
    if (!s) {
        return 0;
    }
    while (s[i]) {
        unsigned char c = (unsigned char)s[i];
        int sl = 1;
        if (c < 0x80) {
            sl = 1;
        } else if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
            sl = 2;
        } else if ((c & 0xF0) == 0xE0) {
            sl = 3;
        } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
            sl = 4;
        } else {
            return 0;
        }
        for (int k = 1; k < sl; k++) {
            if (((unsigned char)s[i + (size_t)k] & 0xC0) != 0x80) {
                return 0;
            }
        }
        i += (size_t)sl;
    }
    return 1;
}

static int ends_ellipsis(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    return n >= ZK_TEXT_ELLIPSIS_LEN &&
           memcmp(s + n - ZK_TEXT_ELLIPSIS_LEN, ZK_TEXT_ELLIPSIS, ZK_TEXT_ELLIPSIS_LEN) == 0;
}

static void test_fit(void)
{
    char dst[64];
    int bad = 0;
    const char *mid = "A \xC2\xB7 B \xE2\x80\xA6 end";
    const char *accent = "A\xC3\xA9\xC3\xA9\xC3\xA9";

    TEQ_I(zk_text_fit(NULL, 8, "abc", 100, cp_width, &bad), -1);
    TEQ_I(zk_text_fit(dst, 0, "abc", 100, cp_width, &bad), -1);
    dst[0] = 'x';
    TEQ_I(zk_text_fit(dst, sizeof dst, "abc", 100, NULL, &bad), -1);
    TEQ_S(dst, "");

    bad = 0;
    TEQ_I(zk_text_fit(dst, sizeof dst, NULL, 100, cp_width, &bad), 0);
    TEQ_S(dst, "");
    TEQ_I(bad, 0);
    TEQ_I(zk_text_fit(dst, sizeof dst, "", 0, cp_width, &bad), 0);
    TEQ_S(dst, "");

    TEQ_I(zk_text_fit(dst, sizeof dst, mid, 1000, cp_width, &bad), 0);
    TEQ_S(dst, mid);
    TCHECK(strstr(dst, "\xC2\xB7") != NULL, "middot kept");
    TCHECK(strstr(dst, ZK_TEXT_ELLIPSIS) != NULL, "source ellipsis kept");

    TEQ_I(zk_text_fit(dst, 8, "ABCDEFGHIJ", 1000, cp_width, &bad), 0);
    TEQ_S(dst, "ABCD" ZK_TEXT_ELLIPSIS);
    TCHECK(ends_ellipsis(dst), "cap 8 ellipsis");
    TCHECK(strlen(dst) + 1 <= 8, "cap 8 length");

    TEQ_I(zk_text_fit(dst, 4, "ABCDEFGHIJ", 1000, cp_width, &bad), 0);
    TEQ_S(dst, ZK_TEXT_ELLIPSIS);
    TEQ_I(zk_text_fit(dst, 3, "ABCDEFGHIJ", 1000, cp_width, &bad), 0);
    TEQ_S(dst, "");
    TEQ_I(zk_text_fit(dst, 1, "ABCDEFGHIJ", 1000, cp_width, &bad), 0);
    TEQ_S(dst, "");

    TEQ_I(zk_text_fit(dst, sizeof dst, "ABCDEFGHIJ", 30, cp_width, &bad), 0);
    TEQ_S(dst, "AB" ZK_TEXT_ELLIPSIS);
    TCHECK(cp_width(dst, strlen(dst), &bad) <= 30, "pixel cap");

    TEQ_I(zk_text_fit(dst, sizeof dst, accent, 20, cp_width, &bad), 0);
    TEQ_S(dst, "A" ZK_TEXT_ELLIPSIS);
    TCHECK(utf8_ok(dst), "accent cut on a boundary");
    TCHECK(strlen(dst) + 1 <= sizeof dst, "accent cap");

    TEQ_I(zk_text_fit(dst, 7, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 1000, cp_width, &bad), 0);
    TEQ_S(dst, "\xC3\xA9" ZK_TEXT_ELLIPSIS);
    TCHECK(utf8_ok(dst), "no mid-sequence cut");

    TEQ_I(zk_text_fit(dst, sizeof dst, "hello\nworld", 1000, cp_width, &bad), 0);
    TEQ_S(dst, "hello" ZK_TEXT_ELLIPSIS);

    TEQ_I(zk_text_fit(dst, sizeof dst, "ABC\xC3", 1000, cp_width, &bad), 0);
    TEQ_S(dst, "ABC" ZK_TEXT_ELLIPSIS);
    TCHECK(utf8_ok(dst), "dangling tail dropped");

    TEQ_I(zk_text_fit(dst, sizeof dst, "\xC0\xAF" "nope", 1000, cp_width, &bad), 0);
    TEQ_S(dst, ZK_TEXT_ELLIPSIS);

    TEQ_I(zk_text_fit(dst, sizeof dst, "ABCDEFGH", 4, suffix_ellipsis_width, NULL), 0);
    TEQ_S(dst, "ABCD" ZK_TEXT_ELLIPSIS);

    TEQ_I(bad, 0);
}

int main(void)
{
    test_fit();
    return zk_test_report("test_text");
}
