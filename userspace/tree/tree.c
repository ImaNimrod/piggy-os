#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PREFIX_MAX 8192

static const char* BAR = "|    ";
static const char* MID = "|--- ";
static const char* END = "`--- ";

static char prefix_buf[PREFIX_MAX];

static void print_tree(size_t count[2], const char* directory_filename, char* prefix_pos, bool print_all) {
    struct dirent** entries;

    int size = scandir(directory_filename, &entries, NULL, alphasort);
    if (size < 0) {
        warn("scandir(%s)", directory_filename);
        return;
    }

    puts(directory_filename);

    int ret = chdir(directory_filename);
    if (ret < 0) {
        warn("chdir(%s)", directory_filename);

        for (int i = 0; i < size; i++) {
            free(entries[i]);
        }

        free(entries);
        return;
    }

    int visible_count = 0;

    for (int i = 0; i < size; i++) {
        const char* name = entries[i]->d_name;

        if (name[0] == '.') {
            if (!print_all || name[1] == '\0' || name[1] == '.') {
                continue;
            }
        }

        visible_count++;
    }

    int visible_index = 0;

    for (int i = 0; i < size;) {
        struct dirent* entry = entries[i++];
        const char* name = entry->d_name;

        if (name[0] == '.') {
            if (!print_all || name[1] == '\0' || name[1] == '.') {
                free(entry);
                continue;
            }
        }

        bool last = ++visible_index == visible_count;

        struct stat st;
        if (lstat(name, &st) < 0) {
            warn("lstat(%s)", name);
            free(entry);
            continue;
        }

        if (last) {
            strcpy(prefix_pos, END);
        } else {
            strcpy(prefix_pos, MID);
        }

        fputs(prefix_buf, stdout);

        if (S_ISLNK(st.st_mode)) {
            char link[PATH_MAX];

            ssize_t nread = readlink(name, link, sizeof(link));
            if (nread < 0) {
                warn("readlink: '%s'", name);
                ret = EXIT_FAILURE;
                continue;
            }

            link[nread] = '\0';

            printf("%s -> %s\n", name, link);
            count[1]++;
        } else if (S_ISDIR(st.st_mode)) {
            char* child_prefix;
            if (last) {
                child_prefix = stpcpy(prefix_pos, "    ");
            } else {
                child_prefix = stpcpy(prefix_pos, BAR);
            }

            if ((size_t) (child_prefix - prefix_buf) >= PREFIX_MAX) {
                warnx("directory tree is too deep");
            } else {
                print_tree(count, name, child_prefix, print_all);
            }

            count[0]++;
        } else {
            puts(name);
            count[1]++;
        }

        free(entry);
    }

    free(entries);

    ret = chdir("..");
    if (ret < 0) {
        err(EXIT_FAILURE, "chdir(..)");
    }
}

static void usage(void) {
    fprintf(stderr, "usage: tree [-a] [DIRECTORY]...\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    bool print_all = false;

    int c;
    while ((c = getopt(argc, argv, "a")) != -1) {
        switch (c) {
            case 'a':
                print_all = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    size_t count[2] = {};

    if (argc < 1) {
        print_tree(count, ".", prefix_buf, print_all);
    } else {
        for (int i = 0; i < argc; i++) {
            print_tree(count, argv[i], prefix_buf, print_all);
        }
    }

    printf("\n%zu directories, %zu files\n", count[0], count[1]);

    return EXIT_SUCCESS;
}
