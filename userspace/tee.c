#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUFSIZE (8 * 1024)

struct out_file {
    int fd;
    const char* filename;
    struct out_file* next;
};

static void add_file(struct out_file** head, int fd, char* filename) {
    struct out_file* node = malloc(sizeof(struct out_file));
    if (node == NULL) {
        err(EXIT_FAILURE, "malloc");
    }

    node->fd = fd;
    node->filename = filename;

    node->next = *head;
    *head = node;
}

static void usage(void) {
    fprintf(stderr, "usage: tee [-a] [FILE]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool append = false;

    int c;
    while ((c = getopt(argc, argv, "a")) != -1) {
        switch (c) {
            case 'a':
                append = true;
                break;
            default:
                usage();
                break;
        }
    }

    argc -= optind;
    argv += optind;

    int oflag = O_WRONLY | O_CREAT;
    if (append) {
        oflag |= O_APPEND;
    } else {
        oflag |= O_TRUNC;
    }

    int ret = EXIT_SUCCESS;

    struct out_file* file_list = NULL;
    add_file(&file_list, STDOUT_FILENO, "stdout");

    for (int i = 0; i < argc; i++) {
        int fd = open(argv[i], oflag, 0777);
        if (fd < 0) {
            warn(argv[i]);
            ret = EXIT_FAILURE;
            continue;
        }

        add_file(&file_list, fd, argv[i]);
    }

    char buf[BUFSIZE];

    struct out_file* iter;

    ssize_t nread;
    while ((nread = read(STDIN_FILENO, buf, BUFSIZE)) > 0) {
        iter = file_list;
        while (iter != NULL) {
            off_t off = 0;
            while (off < nread) {
                ssize_t nwritten = write(iter->fd, buf + off, nread - off);
                if (nwritten <= 0) {
                    warn(iter->filename);
                    ret = EXIT_FAILURE;
                    break;
                }

                off += nwritten;
            }

            iter = iter->next;
        }
    }

    if (nread < 0) {
        err(EXIT_FAILURE, "read(stdin)");
    }

    iter = file_list;
    while (iter != NULL) {
        struct out_file* next = iter->next;

        if (iter->fd != STDOUT_FILENO) {
            close(iter->fd);
        }
        free(iter);

        iter = next;
    }

    return ret;
}
