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

/* Copy whole UTF-8 sequences from src[0, src_n). Stops at NUL, at src_n,
 * or before a sequence that would not fit in cap (including the NUL).
 * Invalid bytes are skipped. A valid sequence that does not fit ends the
 * copy; later bytes are not pulled into the gap. Newlines are kept.
 * Returns 0, or -1 if dst is NULL or cap is 0. NULL src yields "". */
int zk_utf8_ncopy(char *dst, size_t cap, const char *src, size_t src_n);

/* If s ends in a truncated UTF-8 sequence, cut it off. NULL is ignored. */
void zk_utf8_seal(char *s);

/* Fold src into a string the bundled fonts can draw, then copy at most
 * cap-1 bytes on a character boundary. NFC for Latin-1 bases, then dashes,
 * quotes, bullets, spaces, and letters outside the subset. Invalid UTF-8
 * is dropped. NULL src yields "". Returns 0, or -1 if dst is NULL or cap is 0. */
int zk_text_display(char *dst, size_t cap, const char *src);

#endif
