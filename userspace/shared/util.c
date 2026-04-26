#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "util.h"

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

bool get_prompt(void) {
    char c, first;
    first = c = getchar();

    while (c != '\n' && c != EOF) {
        c = getchar();
    }

    return first == 'Y' || first == 'y';
}

bool is_same_file(const struct stat* st1, const struct stat* st2) {
    return st1->st_dev == st2->st_dev && st1->st_ino == st2->st_ino;
}
