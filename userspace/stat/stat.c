#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char* L11 = "Size:";
static const char* L12 = "Blocks:";
static const char* L13 = "IO Block:";
static const char* L21 = "Device:";
static const char* L22 = "Inode:";
static const char* L23 = "Links:";
static const char* L24 = "Device type:";

static inline const char* mode_to_type(mode_t mode) {
    switch (mode & S_IFMT) {
        case S_IFBLK:
            return "Block special file";
        case S_IFCHR:
            return "Character special file";
        case S_IFDIR:
            return "Directory";
        case S_IFREG:
            return "Regular file";
        default:
            return "Unknown file Type";
    }
}

static int num_width(long long val) {
    int width = 1;

    if (val < 0) {
        width++; 
        val = -val;
    }

    while (val >= 10) {
        val /= 10;
        width++;
    }

    return width;
}

static void print_timestamp(const char* type, const struct timespec* ts) {
    struct tm tm;
    if (localtime_r(&ts->tv_sec, &tm) == NULL) {
        err(EXIT_FAILURE, "localtime_r");
    }

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);

    printf("%s: %s.%09ld\n", type, buf, ts->tv_nsec);
}

static void usage(void) {
    fprintf(stderr, "usage: stat [-t] FILE...\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    bool terse = false;

    int c;
    while ((c = getopt(argc, argv, "t")) != -1) {
        switch (c) {
            case 't':
                terse = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing operand");
        usage();
    }

    int ret = EXIT_SUCCESS;

    for (int i = 0; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) < 0) {
            ret = EXIT_FAILURE;
            warn("failed to stat '%s'", argv[i]);
            continue;
        }

        if (terse) {
            printf("%s %ld %ld %04x %04x %lu %u %u %ld %ld %ld %ld\n",
                    argv[i],
                    st.st_size, st.st_blocks, st.st_mode, st.st_dev,
                    st.st_ino, major(st.st_rdev), minor(st.st_rdev),
                    st.st_atime, st.st_mtime, st.st_ctime, st.st_blksize);
        } else {
            int col1_text_width = MAX(strlen(L11), strlen(L21));
            int col2_text_width = MAX(strlen(L12), strlen(L22));
            int col3_text_width = MAX(strlen(L13), strlen(L23));
            int col4_text_width = strlen(L24);

            char dev_str[32], rdev_str[32];
            int dev_str_len = snprintf(dev_str, sizeof(dev_str), "%u,%u",
                    major(st.st_dev), minor(st.st_dev));
            int rdev_str_len = snprintf(rdev_str, sizeof(rdev_str), "%u,%u",
                    major(st.st_rdev), minor(st.st_rdev));

            int col1_value_width = MAX(num_width(st.st_size), dev_str_len);
            int col2_value_width = MAX(num_width(st.st_blocks), num_width(st.st_ino));
            int col3_value_width = MAX(num_width(st.st_blksize), num_width(st.st_nlink));
            int col4_value_width = rdev_str_len;

            printf("  File: %s\n", argv[i]);

            printf("%*s %*ld  %-*s %*ld  %-*s %*ld  %s\n",
                    col1_text_width, L11, col1_value_width, st.st_size,
                    col2_text_width, L12, col2_value_width, st.st_blocks,
                    col3_text_width, L13, col3_value_width, st.st_blksize,
                    mode_to_type(st.st_mode));

            printf("%*s %*s  %-*s %*lu  %-*s %*lu  %-*s %*s\n",
                    col1_text_width, L21, col1_value_width, dev_str,
                    col2_text_width, L22, col2_value_width, st.st_ino,
                    col3_text_width, L23, col3_value_width, st.st_nlink,
                    col4_text_width, L24, col4_value_width, rdev_str);

            print_timestamp("Access", &st.st_atim);
            print_timestamp("Modify", &st.st_mtim);
            print_timestamp("Change", &st.st_ctim);

            if (i < argc - 1) {
                putchar('\n');
            }
        }
    }

    return ret;
}
