#ifndef ZK_CONSOLE_H
#define ZK_CONSOLE_H

/* Best-effort VT cursor hide/show. Writes CSI ?25l / ?25h to ZK_TTY or
 * /dev/tty1. Missing or unwritable tty logs one stderr line and returns.
 * Callers must not use this from a signal handler. */
void zk_console_cursor(int hide);

#endif
