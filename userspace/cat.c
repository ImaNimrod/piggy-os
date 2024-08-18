#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "cat"

#define CAT_BUF_SIZE 1024

static char buf[CAT_BUF_SIZE];

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... [FILE]...\n\nConcatenate FILE(s) to stdout. With no FILE, or when FILE is -, read stdin.\n\n-h\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
}

static int cat(char* path) {
    int ret = EXIT_SUCCESS;

    int fd;
    if (!strcmp(path, "-")) {
        fd = STDIN_FILENO;
    } else {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            fputs(PROGRAM_NAME ": ", stderr);
            perror(path);
            ret = EXIT_FAILURE;
            goto end;
        }
    }

    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            fputs(PROGRAM_NAME ": ", stderr);
            perror(path);
            ret = EXIT_FAILURE;
            goto end;
        }
    }

    if (n < 0) {
        fputs(PROGRAM_NAME ": ", stderr);
        perror(path);
        ret = EXIT_FAILURE;
        goto end;
    }

end:
    close(fd);
    return ret;
}

int main(int argc, char** argv) {
    int c;
    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
            case 'h': 
                help();
                break;
            case '?':
                error();
                break;
        }
    }

    int ret = EXIT_SUCCESS;

    if (optind == argc) {
        ret = cat("-");
    } else {
        for (int i = optind; i < argc; i++) {
            ret |= cat(argv[i]);
        }
    }

    return ret;
}
