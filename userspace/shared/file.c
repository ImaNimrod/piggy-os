#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "file.h"

bool file_copy(int src_fd, const char* src_path, int dest_fd, const char* dest_path) {
    for (;;) {
        char buf[4096];

        ssize_t nread = read(src_fd, buf, sizeof(buf));
        if (nread < 0) {
            warn("read(%s)", src_path);
            return false;
        } else if (nread == 0) {
            return true;
        }

        while (nread > 0) {
            ssize_t nwritten = write(dest_fd, buf, nread);
            if (nwritten < 0) {
                warn("write(%s)", dest_path);
                return false;
            }

            nread -= nwritten;
        }
    }
}
