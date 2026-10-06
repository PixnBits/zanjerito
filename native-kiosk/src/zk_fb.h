#ifndef ZK_FB_H
#define ZK_FB_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    unsigned xres, yres, bpp, line_length;
    unsigned xoffset, yoffset;
    unsigned r_off, r_len, g_off, g_len, b_off, b_len;
    size_t smem_len;
} zk_fb_geom_t;

typedef enum {
    ZK_FB_FMT_NONE = 0,
    ZK_FB_FMT_RGB565,
    ZK_FB_FMT_XRGB8888
} zk_fb_fmt_t;

/* 0 on a supported layout. Otherwise ZK_FB_FMT_NONE and a message in err
 * (when errlen > 0) naming bpp and channel offsets/lengths. */
zk_fb_fmt_t zk_fb_pick_format(const zk_fb_geom_t *g, char *err, size_t errlen);

/* Read geometry with FBIOGET_FSCREENINFO and FBIOGET_VSCREENINFO.
 * xoffset and yoffset come from fb_var_screeninfo.
 *
 * ZK_FB_FAKE is a TEST-ONLY hook, same idea as ZAN_SYSFS_BACKLIGHT_ROOT.
 * Production leaves it unset. If the ioctl fails, the path is not an fbdev
 * device (character device, major 29; stat follows symlinks), and ZK_FB_FAKE
 * is WxHxBPP or WxHxBPPxSTRIDE (for example 800x480x16 or 800x480x32x3328),
 * geometry comes from that string. STRIDE is line_length in bytes and must
 * be at least W*BPP/8. Omitted, line_length is W*BPP/8. Fake xoffset and
 * yoffset are 0. 16 bpp is RGB565 (R 11/5, G 5/6, B 0/5). 32 bpp is
 * XRGB8888 (R 16/8, G 8/8, B 0/8). Any other bpp gets lengths 8 at offsets
 * 16/8/0 so zk_fb_pick_format rejects it. The regular file must be at least
 * line_length * yres bytes. A real fbdev device never uses the fake.
 *
 * Returns 0 on success, -1 on error (err receives a message when errlen > 0).
 */
int zk_fb_probe(int fd, const char *path, zk_fb_geom_t *g, char *err, size_t errlen);

/* 1 when mode is a character device and rdev's major is 29 (Linux fbdev). */
int zk_fb_mode_is_fbdev(mode_t mode, dev_t rdev);

/* stat follows symlinks. 1 when the path is an fbdev device, else 0. */
int zk_fb_path_is_fbdev(const char *path);

/* Byte offset of visible pixel (x, y):
 * (yoffset + y) * line_length + (xoffset + x) * (bpp / 8). */
size_t zk_fb_pixel_offset(const zk_fb_geom_t *g, unsigned x, unsigned y);

/* Expand one pixel to 8-bit channels. RGB565 is little-endian. XRGB8888
 * bytes are B, G, R, X. */
void zk_fb_px_rgb(zk_fb_fmt_t f, const uint8_t *px, uint8_t *r, uint8_t *g, uint8_t *b);

/* Visible frame to packed RGB888. line_length may be wider than the pixels.
 * xoffset and yoffset select the window inside the buffer. nonblack_out
 * counts pixels whose r|g|b is not 0. Returns 0, or -1. */
int zk_fb_to_rgb(zk_fb_fmt_t f, const zk_fb_geom_t *g, const uint8_t *src,
                 uint8_t *rgb_out, size_t *nonblack_out);

#endif
