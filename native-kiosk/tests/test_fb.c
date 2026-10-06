#include "zk_fb.h"
#include "zk_test.h"

#include <stdlib.h>
#include <unistd.h>

static zk_fb_geom_t geom(unsigned w, unsigned h, unsigned bpp, unsigned line, unsigned ro,
                         unsigned rl, unsigned go, unsigned gl, unsigned bo, unsigned bl,
                         size_t smem)
{
    zk_fb_geom_t g;

    memset(&g, 0, sizeof g);
    g.xres = w;
    g.yres = h;
    g.bpp = bpp;
    g.line_length = line;
    g.r_off = ro;
    g.r_len = rl;
    g.g_off = go;
    g.g_len = gl;
    g.b_off = bo;
    g.b_len = bl;
    g.smem_len = smem;
    return g;
}

static void expect_none(const zk_fb_geom_t *g, const char *bpp_txt)
{
    char err[320];
    zk_fb_fmt_t f;

    err[0] = '\0';
    f = zk_fb_pick_format(g, err, sizeof err);
    TEQ_I(f, ZK_FB_FMT_NONE);
    TCHECK(err[0] != '\0', "empty err");
    TCHECK(strstr(err, bpp_txt) != NULL, "err \"%s\" missing %s", err, bpp_txt);
    TCHECK(strstr(err, "16 bpp RGB565") != NULL, "err \"%s\"", err);
    TCHECK(strstr(err, "32 bpp XRGB8888") != NULL, "err \"%s\"", err);
}

static void test_pick(void)
{
    zk_fb_geom_t g;
    char err[64];
    size_t smem16 = (size_t)1600 * 480;
    size_t smem32 = (size_t)3200 * 480;

    g = geom(800, 480, 16, 1600, 11, 5, 5, 6, 0, 5, smem16);
    err[0] = 'x';
    TEQ_I(zk_fb_pick_format(&g, err, sizeof err), ZK_FB_FMT_RGB565);

    g = geom(800, 480, 32, 3200, 16, 8, 8, 8, 0, 8, smem32);
    TEQ_I(zk_fb_pick_format(&g, err, sizeof err), ZK_FB_FMT_XRGB8888);

    g = geom(800, 480, 24, 2400, 16, 8, 8, 8, 0, 8, (size_t)2400 * 480);
    expect_none(&g, "24 bpp");

    g = geom(800, 480, 8, 800, 16, 8, 8, 8, 0, 8, (size_t)800 * 480);
    expect_none(&g, "8 bpp");

    g = geom(800, 480, 16, 1600, 10, 5, 5, 5, 0, 5, smem16);
    expect_none(&g, "16 bpp");

    g = geom(800, 480, 32, 3200, 0, 8, 8, 8, 16, 8, smem32);
    expect_none(&g, "32 bpp");

    g = geom(800, 480, 16, 100, 11, 5, 5, 6, 0, 5, (size_t)100 * 480);
    expect_none(&g, "16 bpp");

    g = geom(800, 480, 16, 1600, 11, 5, 5, 6, 0, 5, 1600);
    expect_none(&g, "16 bpp");

    g = geom(0, 480, 16, 1600, 11, 5, 5, 6, 0, 5, smem16);
    expect_none(&g, "16 bpp");
}

static void put565(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void expect_px(zk_fb_fmt_t f, const uint8_t *px, int r, int g, int b)
{
    uint8_t rr = 1;
    uint8_t gg = 1;
    uint8_t bb = 1;

    zk_fb_px_rgb(f, px, &rr, &gg, &bb);
    TEQ_I(rr, r);
    TEQ_I(gg, g);
    TEQ_I(bb, b);
}

static void test_px(void)
{
    uint8_t px[4];
    uint8_t xrgb[4] = {0x10, 0x20, 0x30, 0xFF};

    put565(px, 0xF800);
    expect_px(ZK_FB_FMT_RGB565, px, 255, 0, 0);
    put565(px, 0x07E0);
    expect_px(ZK_FB_FMT_RGB565, px, 0, 255, 0);
    put565(px, 0x001F);
    expect_px(ZK_FB_FMT_RGB565, px, 0, 0, 255);
    put565(px, 0xFFFF);
    expect_px(ZK_FB_FMT_RGB565, px, 255, 255, 255);
    put565(px, 0);
    expect_px(ZK_FB_FMT_RGB565, px, 0, 0, 0);
    expect_px(ZK_FB_FMT_XRGB8888, xrgb, 0x30, 0x20, 0x10);
}

static void test_to_rgb(void)
{
    /* 4x2 RGB565, line_length 16 (8 bytes of pixels + 8 bytes of padding). */
    uint8_t src[32];
    uint8_t rgb[4 * 2 * 3];
    zk_fb_geom_t g;
    size_t nonblack = 99;
    int i;

    memset(src, 0xFF, sizeof src);
    put565(src + 0, 0xF800);
    put565(src + 2, 0x0000);
    put565(src + 4, 0x07E0);
    put565(src + 6, 0x0000);
    put565(src + 16, 0x001F);
    put565(src + 18, 0xFFFF);
    put565(src + 20, 0x0000);
    put565(src + 22, 0x0000);
    g = geom(4, 2, 16, 16, 11, 5, 5, 6, 0, 5, sizeof src);
    TEQ_I(zk_fb_to_rgb(ZK_FB_FMT_RGB565, &g, src, rgb, &nonblack), 0);
    TEQ_I((int)nonblack, 4);
    TEQ_I(rgb[0], 255);
    TEQ_I(rgb[1], 0);
    TEQ_I(rgb[2], 0);
    TEQ_I(rgb[2 * 3 + 0], 0);
    TEQ_I(rgb[2 * 3 + 1], 255);
    TEQ_I(rgb[2 * 3 + 2], 0);
    TEQ_I(rgb[4 * 3 + 0], 0);
    TEQ_I(rgb[4 * 3 + 1], 0);
    TEQ_I(rgb[4 * 3 + 2], 255);
    TEQ_I(rgb[5 * 3 + 0], 255);
    TEQ_I(rgb[5 * 3 + 1], 255);
    TEQ_I(rgb[5 * 3 + 2], 255);
    for (i = 0; i < 4 * 2; i++) {
        int on = (rgb[i * 3] | rgb[i * 3 + 1] | rgb[i * 3 + 2]) != 0;
        int expect = (i == 0 || i == 2 || i == 4 || i == 5);
        TEQ_I(on, expect);
    }
}

static int make_file(char *path, size_t pathsz, size_t nbytes, int *fd_out)
{
    char tmpl[] = "/tmp/zkfbXXXXXX";
    int fd;

    if (pathsz < sizeof tmpl) {
        return -1;
    }
    fd = mkstemp(tmpl);
    if (fd < 0) {
        return -1;
    }
    if (ftruncate(fd, (off_t)nbytes) != 0) {
        close(fd);
        unlink(tmpl);
        return -1;
    }
    memcpy(path, tmpl, sizeof tmpl);
    *fd_out = fd;
    return 0;
}

static void test_probe(void)
{
    char path[64];
    char err[256];
    zk_fb_geom_t g;
    int fd = -1;
    size_t n16 = (size_t)800 * 480 * 2;
    size_t n32 = (size_t)800 * 480 * 4;
    size_t n24 = (size_t)800 * 480 * 3;

    unsetenv("ZK_FB_FAKE");
    TCHECK(make_file(path, sizeof path, n16, &fd) == 0, "temp 16");
    err[0] = '\0';
    TCHECK(zk_fb_probe(fd, path, &g, err, sizeof err) != 0, "unset should fail");
    TCHECK(err[0] != '\0', "empty unset err");
    TCHECK(strstr(err, "ZK_FB_FAKE") != NULL, "err \"%s\"", err);

    TCHECK(setenv("ZK_FB_FAKE", "800x480x16", 1) == 0, "setenv 16");
    err[0] = '\0';
    TCHECK(zk_fb_probe(fd, path, &g, err, sizeof err) == 0, "probe 16: %s", err);
    TEQ_I(g.xres, 800);
    TEQ_I(g.yres, 480);
    TEQ_I(g.bpp, 16);
    TEQ_I(g.line_length, 1600);
    TEQ_I(g.r_off, 11);
    TEQ_I(g.r_len, 5);
    TEQ_I(g.g_off, 5);
    TEQ_I(g.g_len, 6);
    TEQ_I(g.b_off, 0);
    TEQ_I(g.b_len, 5);
    TEQ_I((int)g.smem_len, (int)n16);
    TEQ_I(zk_fb_pick_format(&g, err, sizeof err), ZK_FB_FMT_RGB565);

    /* The string is a device path. The fd is still the temp file, so this
     * does not open a real framebuffer. The fake must not apply. */
    err[0] = '\0';
    TCHECK(zk_fb_probe(fd, "/dev/fb0", &g, err, sizeof err) != 0, "dev path used fake");
    TCHECK(err[0] != '\0', "empty dev err");
    close(fd);
    unlink(path);
    fd = -1;

    TCHECK(make_file(path, sizeof path, 100, &fd) == 0, "temp short");
    TCHECK(setenv("ZK_FB_FAKE", "800x480x16", 1) == 0, "setenv short");
    err[0] = '\0';
    TCHECK(zk_fb_probe(fd, path, &g, err, sizeof err) != 0, "short file accepted");
    TCHECK(strstr(err, "short") != NULL, "err \"%s\"", err);
    close(fd);
    unlink(path);
    fd = -1;

    TCHECK(make_file(path, sizeof path, n32, &fd) == 0, "temp 32");
    TCHECK(setenv("ZK_FB_FAKE", "800x480x32", 1) == 0, "setenv 32");
    err[0] = '\0';
    TCHECK(zk_fb_probe(fd, path, &g, err, sizeof err) == 0, "probe 32: %s", err);
    TEQ_I(g.bpp, 32);
    TEQ_I(g.line_length, 3200);
    TEQ_I(g.r_off, 16);
    TEQ_I(g.r_len, 8);
    TEQ_I(g.g_off, 8);
    TEQ_I(g.g_len, 8);
    TEQ_I(g.b_off, 0);
    TEQ_I(g.b_len, 8);
    TEQ_I((int)g.smem_len, (int)n32);
    TEQ_I(zk_fb_pick_format(&g, err, sizeof err), ZK_FB_FMT_XRGB8888);
    close(fd);
    unlink(path);
    fd = -1;

    TCHECK(make_file(path, sizeof path, n24, &fd) == 0, "temp 24");
    TCHECK(setenv("ZK_FB_FAKE", "800x480x24", 1) == 0, "setenv 24");
    err[0] = '\0';
    TCHECK(zk_fb_probe(fd, path, &g, err, sizeof err) == 0, "probe 24: %s", err);
    TEQ_I(g.bpp, 24);
    TEQ_I(g.r_off, 16);
    TEQ_I(g.r_len, 8);
    TEQ_I(g.g_off, 8);
    TEQ_I(g.g_len, 8);
    TEQ_I(g.b_off, 0);
    TEQ_I(g.b_len, 8);
    expect_none(&g, "24 bpp");
    close(fd);
    unlink(path);

    unsetenv("ZK_FB_FAKE");
}

int main(void)
{
    test_pick();
    test_px();
    test_to_rgb();
    test_probe();
    return zk_test_report("test_fb");
}
