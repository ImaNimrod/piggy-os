#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static inline const char* mode_to_type(mode_t mode) {
    switch (mode & S_IFMT) {
        case S_IFBLK:
            return "block special file";
        case S_IFCHR:
            return "character special file";
        case S_IFDIR:
            return "directory";
        case S_IFREG:
            return "regular file";
        default:
            return "unknown file";
    }
}

static void print_timestamp(struct timespec* ts) {
    struct tm* tm = localtime(&ts->tv_sec);
    printf("%04d-%02d-%02d %02d:%02d:%02d.%09ld\n",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, ts->tv_nsec);
}

static void usage(void) {
    fprintf(stderr, "usage: stat FILE...\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing operand");
        usage();
    }

    int ret = EXIT_SUCCESS;

    struct stat st;

    for (int i = 0; i < argc; i++) {
        ret = stat(argv[i], &st);
        if (ret < 0) {
            warn("cannot stat '%s'", argv[i]);
            continue;
        }

        printf("  File: %s\n", argv[i]);
        printf("  Size: %-15ld Blocks: %-10ld IO Block: %-6ld %s\n",
                st.st_size, st.st_blocks, st.st_blksize, mode_to_type(st.st_mode));
        printf("Device: %u,%-5u Inode: %-11lu Device type: %u,%u\n",
                major(st.st_dev), minor(st.st_dev), st.st_ino, major(st.st_rdev), minor(st.st_rdev));
        print_timestamp(&st.st_atim);
        print_timestamp(&st.st_mtim);
        print_timestamp(&st.st_ctim);
    }

    return ret;
}
