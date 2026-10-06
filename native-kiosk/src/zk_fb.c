#include "zk_fb.h"

#include <errno.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

static void set_err(char *err, size_t errlen, const char *msg)
{
    if (!err || errlen == 0) {
        return;
    }
    snprintf(err, errlen, "%s", msg ? msg : "");
}

static int path_is_dev_fb(const char *path)
{
    size_t i;

    if (!path || strncmp(path, "/dev/fb", 7) != 0 || path[7] == '\0') {
        return 0;
    }
    for (i = 7; path[i] != '\0'; i++) {
        if (path[i] < '0' || path[i] > '9') {
            return 0;
        }
    }
    return 1;
}

static int is_rgb565(const zk_fb_geom_t *g)
{
    return g->bpp == 16 && g->r_off == 11 && g->r_len == 5 && g->g_off == 5 && g->g_len == 6 &&
           g->b_off == 0 && g->b_len == 5;
}

static int is_xrgb8888(const zk_fb_geom_t *g)
{
    return g->bpp == 32 && g->r_off == 16 && g->r_len == 8 && g->g_off == 8 && g->g_len == 8 &&
           g->b_off == 0 && g->b_len == 8;
}

static void format_err(char *err, size_t errlen, const zk_fb_geom_t *g, const char *why)
{
    char buf[384];

    if (!g) {
        set_err(err, errlen, "missing geometry; build supports 16 bpp RGB565 and 32 bpp XRGB8888");
        return;
    }
    snprintf(buf, sizeof buf,
             "%s%u bpp channels R%u/%u G%u/%u B%u/%u; build supports 16 bpp RGB565 and 32 bpp XRGB8888",
             why ? why : "", g->bpp, g->r_off, g->r_len, g->g_off, g->g_len, g->b_off, g->b_len);
    set_err(err, errlen, buf);
}

zk_fb_fmt_t zk_fb_pick_format(const zk_fb_geom_t *g, char *err, size_t errlen)
{
    uint64_t min_line;
    const char *why;

    if (!g) {
        format_err(err, errlen, NULL, NULL);
        return ZK_FB_FMT_NONE;
    }
    why = NULL;
    if (g->xres == 0 || g->yres == 0) {
        why = "zero resolution; ";
    } else if (g->bpp < 8 || (g->bpp % 8u) != 0) {
        why = "bpp not a whole number of bytes; ";
    } else {
        min_line = ((uint64_t)g->xres * (uint64_t)g->bpp) / 8u;
        if ((uint64_t)g->line_length < min_line) {
            why = "line_length short; ";
        } else if ((uint64_t)g->line_length * (uint64_t)g->yres > (uint64_t)g->smem_len) {
            why = "smem_len short; ";
        } else if (!is_rgb565(g) && !is_xrgb8888(g)) {
            why = "unsupported layout; ";
        }
    }
    if (why) {
        format_err(err, errlen, g, why);
        return ZK_FB_FMT_NONE;
    }
    if (err && errlen > 0) {
        err[0] = '\0';
    }
    return is_rgb565(g) ? ZK_FB_FMT_RGB565 : ZK_FB_FMT_XRGB8888;
}

static int parse_fake(const char *spec, unsigned *w, unsigned *h, unsigned *bpp)
{
    unsigned ww;
    unsigned hh;
    unsigned bb;
    char tail;

    if (!spec || !spec[0]) {
        return -1;
    }
    if (sscanf(spec, "%ux%ux%u%c", &ww, &hh, &bb, &tail) != 3) {
        return -1;
    }
    if (ww == 0 || hh == 0 || bb == 0 || bb > 64u) {
        return -1;
    }
    *w = ww;
    *h = hh;
    *bpp = bb;
    return 0;
}

static void fill_fake_channels(zk_fb_geom_t *g, unsigned bpp)
{
    g->bpp = bpp;
    if (bpp == 16) {
        g->r_off = 11;
        g->r_len = 5;
        g->g_off = 5;
        g->g_len = 6;
        g->b_off = 0;
        g->b_len = 5;
        return;
    }
    if (bpp == 32) {
        g->r_off = 16;
        g->r_len = 8;
        g->g_off = 8;
        g->g_len = 8;
        g->b_off = 0;
        g->b_len = 8;
        return;
    }
    /* Any other depth: a layout pick_format rejects on bpp. */
    g->r_off = 16;
    g->r_len = 8;
    g->g_off = 8;
    g->g_len = 8;
    g->b_off = 0;
    g->b_len = 8;
}

static int probe_fake(int fd, zk_fb_geom_t *g, char *err, size_t errlen)
{
    const char *spec;
    unsigned w;
    unsigned h;
    unsigned bpp;
    uint64_t line;
    uint64_t smem;
    struct stat st;

    /* TEST-ONLY. Production leaves ZK_FB_FAKE unset. Real /dev/fbN never
     * reaches here. */
    spec = getenv("ZK_FB_FAKE");
    if (!spec || !spec[0]) {
        set_err(err, errlen, "framebuffer ioctl failed; ZK_FB_FAKE is unset");
        return -1;
    }
    if (parse_fake(spec, &w, &h, &bpp) != 0) {
        set_err(err, errlen, "ZK_FB_FAKE: expected WxHxBPP");
        return -1;
    }
    line = ((uint64_t)w * (uint64_t)bpp) / 8u;
    smem = line * (uint64_t)h;
    if (line > 0xffffffffu || smem > (uint64_t)(size_t)-1) {
        set_err(err, errlen, "ZK_FB_FAKE: frame is too large");
        return -1;
    }
    if (fstat(fd, &st) != 0) {
        set_err(err, errlen, "ZK_FB_FAKE: fstat failed");
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        set_err(err, errlen, "ZK_FB_FAKE: not a regular file");
        return -1;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size < smem) {
        set_err(err, errlen, "ZK_FB_FAKE: file is shorter than the frame");
        return -1;
    }
    memset(g, 0, sizeof *g);
    g->xres = w;
    g->yres = h;
    fill_fake_channels(g, bpp);
    g->line_length = (unsigned)line;
    g->smem_len = (size_t)smem;
    return 0;
}

int zk_fb_probe(int fd, const char *path, zk_fb_geom_t *g, char *err, size_t errlen)
{
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;

    if (!g) {
        set_err(err, errlen, "missing geometry");
        return -1;
    }
    memset(g, 0, sizeof *g);
    if (fd >= 0 && ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == 0 &&
        ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == 0) {
        g->xres = vinfo.xres;
        g->yres = vinfo.yres;
        g->bpp = vinfo.bits_per_pixel;
        g->line_length = finfo.line_length;
        g->r_off = vinfo.red.offset;
        g->r_len = vinfo.red.length;
        g->g_off = vinfo.green.offset;
        g->g_len = vinfo.green.length;
        g->b_off = vinfo.blue.offset;
        g->b_len = vinfo.blue.length;
        g->smem_len = finfo.smem_len;
        return 0;
    }
    if (path_is_dev_fb(path)) {
        set_err(err, errlen, "FBIOGET_FSCREENINFO/FBIOGET_VSCREENINFO failed");
        return -1;
    }
    return probe_fake(fd, g, err, errlen);
}

static uint8_t exp5(unsigned v)
{
    v &= 31u;
    return (uint8_t)((v << 3) | (v >> 2));
}

static uint8_t exp6(unsigned v)
{
    v &= 63u;
    return (uint8_t)((v << 2) | (v >> 4));
}

void zk_fb_px_rgb(zk_fb_fmt_t f, const uint8_t *px, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint16_t v;

    if (!r || !g || !b) {
        return;
    }
    if (!px || f == ZK_FB_FMT_NONE) {
        *r = 0;
        *g = 0;
        *b = 0;
        return;
    }
    if (f == ZK_FB_FMT_RGB565) {
        v = (uint16_t)px[0] | (uint16_t)((uint16_t)px[1] << 8);
        *r = exp5(v >> 11);
        *g = exp6(v >> 5);
        *b = exp5(v);
        return;
    }
    if (f == ZK_FB_FMT_XRGB8888) {
        *b = px[0];
        *g = px[1];
        *r = px[2];
        return;
    }
    *r = 0;
    *g = 0;
    *b = 0;
}

static unsigned fmt_bytes(zk_fb_fmt_t f)
{
    if (f == ZK_FB_FMT_RGB565) {
        return 2;
    }
    if (f == ZK_FB_FMT_XRGB8888) {
        return 4;
    }
    return 0;
}

int zk_fb_to_rgb(zk_fb_fmt_t f, const zk_fb_geom_t *g, const uint8_t *src, uint8_t *rgb_out,
                 size_t *nonblack_out)
{
    unsigned bpp;
    unsigned y;
    unsigned x;
    size_t nblack;

    if (nonblack_out) {
        *nonblack_out = 0;
    }
    bpp = fmt_bytes(f);
    if (!g || !src || !rgb_out || bpp == 0) {
        return -1;
    }
    if ((f == ZK_FB_FMT_RGB565 && g->bpp != 16) || (f == ZK_FB_FMT_XRGB8888 && g->bpp != 32)) {
        return -1;
    }
    if (g->xres == 0 || g->yres == 0) {
        return -1;
    }
    if ((uint64_t)g->line_length < (uint64_t)g->xres * bpp) {
        return -1;
    }
    if ((uint64_t)g->line_length * (uint64_t)g->yres > (uint64_t)g->smem_len) {
        return -1;
    }
    nblack = 0;
    for (y = 0; y < g->yres; y++) {
        const uint8_t *row = src + (size_t)y * g->line_length;
        uint8_t *dst = rgb_out + (size_t)y * g->xres * 3u;
        for (x = 0; x < g->xres; x++) {
            uint8_t r;
            uint8_t gv;
            uint8_t b;
            zk_fb_px_rgb(f, row + (size_t)x * bpp, &r, &gv, &b);
            dst[x * 3u + 0] = r;
            dst[x * 3u + 1] = gv;
            dst[x * 3u + 2] = b;
            if ((r | gv | b) != 0) {
                nblack++;
            }
        }
    }
    if (nonblack_out) {
        *nonblack_out = nblack;
    }
    return 0;
}
