#ifndef ZK_TEST_H
#define ZK_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int zk_fails;
static int zk_passes;

#define TCHECK(cond, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        zk_fails++; \
    } else { \
        zk_passes++; \
    } \
} while (0)

#define TEQ_I(a, b) TCHECK((a) == (b), "expected %d == %d", (int)(a), (int)(b))
#define TEQ_S(a, b) TCHECK(strcmp((a), (b)) == 0, "expected \"%s\" == \"%s\"", (const char *)(a), (const char *)(b))
#define TEQ_D(a, b) do { \
    double _za = (double)(a), _zb = (double)(b); \
    double _zd = _za > _zb ? _za - _zb : _zb - _za; \
    TCHECK(_zd < 1e-9, "expected %g == %g", _za, _zb); \
} while (0)

static int zk_test_report(const char *name)
{
    printf("%s: %d passed, %d failed\n", name, zk_passes, zk_fails);
    return zk_fails ? 1 : 0;
}

static char *zk_read_file(const char *path) __attribute__((unused));
static char *zk_read_file(const char *path)
{
    FILE *f;
    long n;
    char *b;
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n < 0 || n > 1 << 20) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    b = (char *)malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    if (n > 0 && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return NULL;
    }
    b[n] = 0;
    fclose(f);
    return b;
}

static char *zk_fixture(const char *scenario, const char *file) __attribute__((unused));
static char *zk_fixture(const char *scenario, const char *file)
{
    char path[512];
    const char *roots[] = {
        "tests/fixtures",
        "native-kiosk/tests/fixtures",
        "../tests/fixtures",
        NULL
    };
    int i;
    for (i = 0; roots[i]; i++) {
        char *b;
        snprintf(path, sizeof(path), "%s/%s/%s", roots[i], scenario, file);
        b = zk_read_file(path);
        if (b) {
            return b;
        }
    }
    fprintf(stderr, "missing fixture %s/%s\n", scenario, file);
    return NULL;
}

#endif
