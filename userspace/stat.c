#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PROGRAM_NAME "stat"

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... FILE...\n\nDisplay metadata for each FILE.\n\n-t\tprint the information in terse form (file, size, block count, real dev, inode, dev, atime, mtime, ctime, block size)\n-h\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
}

static const char* file_type_name(mode_t mode) {
    switch (mode & S_IFMT) {
        case S_IFREG: return "regular file";
        case S_IFDIR: return "directory";
        case S_IFCHR: return "character device file";
        case S_IFBLK: return "block device file";
    }
    return "unknown file type";
}

int main(int argc, char** argv) {
    bool print_terse = false;

    int c;
    while ((c = getopt(argc, argv, "ht")) != -1) {
        switch (c) {
            case 't':
                print_terse = true;
                break;
            case 'h':
                help();
                break;
            case '?':
                error();
                break;
        }
    }

    if (optind == argc) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        error();
    }

    int ret = EXIT_SUCCESS;

    struct stat s;

    for (int i = optind; i < argc; i++) {
        if (stat(argv[i], &s) < 0) {
            fprintf(stderr, PROGRAM_NAME ": cannot stat '%s': %s\n", argv[i], strerror(errno));
            ret = EXIT_FAILURE;
            continue;
        }

        if (print_terse) {
            printf("%s %ld %ld %04x %lu %04x %ld %ld %ld %ld\n",
                    argv[i], s.st_size, s.st_blocks, s.st_rdev, s.st_ino, s.st_dev,
                    s.st_atim.tv_sec, s.st_mtim.tv_sec, s.st_ctim.tv_sec, s.st_blksize);
        } else {
            printf("  file: %s (%s)\n  size: %-8ld  blocks: %-8ld  block size: %-8ld\n inode: %-9lu device: %u,%u   ",
                    argv[i], file_type_name(s.st_mode), s.st_size, s.st_blocks, s.st_blksize, s.st_ino, major(s.st_dev), minor(s.st_dev));

            if (S_ISCHR(s.st_mode) || S_ISBLK(s.st_mode)) {
                printf("device no.: %u,%u\n", major(s.st_rdev), minor(s.st_rdev));
            } else {
                putchar('\n');
            }

            char buf[50];
            struct tm* tm;

            tm = localtime(&s.st_atim.tv_sec);
            strftime(buf, sizeof(buf), "%Y-%m-%d %X", tm);
            printf("access: %s\n", buf);

            tm = localtime(&s.st_mtim.tv_sec);
            strftime(buf, sizeof(buf), "%Y-%m-%d %X", tm);
            printf("modify: %s\n", buf);

            tm = localtime(&s.st_ctim.tv_sec);
            strftime(buf, sizeof(buf), "%Y-%m-%d %X", tm);
            printf("create: %s\n", buf);
        }
    }

    return ret;
}
