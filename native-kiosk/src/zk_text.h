#ifndef ZK_TEXT_H
#define ZK_TEXT_H

#include <stddef.h>

#define ZK_TEXT_ELLIPSIS "\xE2\x80\xA6"
#define ZK_TEXT_ELLIPSIS_LEN 3

/* Copy src into dst (cap includes the NUL) so width_px of the result is
 * <= max_px. The full string is kept when it already fits in both pixels
 * and cap. Otherwise the longest well-formed prefix that fits with a
 * trailing U+2026 is used. Never splits a UTF-8 sequence. A newline or an
 * invalid sequence ends the source (the kept prefix still gets an ellipsis).
 * width_px is called with one contiguous NUL-terminated candidate; nbytes
 * is its length and text[nbytes] is NUL.
 * Returns 0 on success, including truncation to empty. Returns -1 if dst is
 * NULL, cap is 0, or width_px is NULL (dst is cleared when cap > 0).
 * NULL src is an empty string. */
int zk_text_fit(char *dst, size_t cap, const char *src, int max_px,
                int (*width_px)(const char *text, size_t nbytes, void *user), void *user);

#endif
