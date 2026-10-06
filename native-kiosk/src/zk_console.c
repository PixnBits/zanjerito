#include "zk_console.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void zk_console_cursor(int hide)
{
    const char *path;
    const char *seq;
    int fd;
    ssize_t n;
    size_t len;

    path = getenv("ZK_TTY");
    if (!path || !path[0]) {
        path = "/dev/tty1";
    }
    fd = open(path, O_WRONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "console cursor: %s: %s\n", path, strerror(errno));
        return;
    }
    seq = hide ? "\033[?25l" : "\033[?25h";
    len = 6;
    n = write(fd, seq, len);
    if (n < 0 || (size_t)n != len) {
        fprintf(stderr, "console cursor: %s: %s\n", path,
                n < 0 ? strerror(errno) : "short write");
    }
    close(fd);
}
