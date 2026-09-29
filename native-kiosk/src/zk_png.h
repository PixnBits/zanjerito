#ifndef ZK_PNG_H
#define ZK_PNG_H

#include <stdint.h>

/* src is XRGB8888 as stored by LVGL: B, G, R, X per pixel. */
int zk_png_write_xrgb8888(const char *path, const uint8_t *src, int w, int h, int stride);

#endif
