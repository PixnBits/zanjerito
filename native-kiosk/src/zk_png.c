#include "zk_png.h"

#include <stdlib.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wold-style-declaration"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wconversion"
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

int zk_png_write_xrgb8888(const char *path, const uint8_t *src, int w, int h, int stride)
{
    uint8_t *rgb;
    int y;
    int x;
    int rc;

    if (!path || !path[0] || !src || w <= 0 || h <= 0 || stride < w * 4) {
        return -1;
    }
    rgb = malloc((size_t)w * (size_t)h * 3u);
    if (!rgb) {
        return -1;
    }
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * (size_t)stride;
        uint8_t *dst = rgb + (size_t)y * (size_t)w * 3u;
        for (x = 0; x < w; x++) {
            dst[x * 3 + 0] = row[x * 4 + 2];
            dst[x * 3 + 1] = row[x * 4 + 1];
            dst[x * 3 + 2] = row[x * 4 + 0];
        }
    }
    rc = stbi_write_png(path, w, h, 3, rgb, w * 3);
    free(rgb);
    return rc == 0 ? -1 : 0;
}

int zk_png_write_rgb888(const char *path, const uint8_t *rgb, int w, int h)
{
    int rc;

    if (!path || !path[0] || !rgb || w <= 0 || h <= 0) {
        return -1;
    }
    rc = stbi_write_png(path, w, h, 3, rgb, w * 3);
    return rc == 0 ? -1 : 0;
}
