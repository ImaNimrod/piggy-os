#include <sys/ioctl.h> 
#include <sys/stat.h> 

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <termios.h>
#include <time.h> 
#include <unistd.h> 

#define SIX_MONTHS (time_t) (182 * 24 * 60 * 60)

#define RECENT_TIME_FORMAT  "%b %e %H:%M"
#define OLD_TIME_FORMAT     "%b %e  %Y"

struct directory_entry {
    char* name;
    char* link_target;
    struct stat stat;
};

struct directory_listing {
    size_t capacity;
    size_t entry_count;
    struct directory_entry** entries;
};

enum {
    LINK_DEREF_MODE_NONE,
    LINK_DEREF_MODE_CLI,
    LINK_DEREF_MODE_ALL,
};

static bool filter_all(const char* name);
static bool filter_almost_all(const char* name);
static bool filter_default(const char* name);

static void output_columns(struct directory_listing* listing);
static void output_long(struct directory_listing* listing);
static void output_oneline(struct directory_listing* listing);

static int sort_name(struct directory_entry** entry1, struct directory_entry** entry2);
static int sort_size(struct directory_entry** entry1, struct directory_entry** entry2);
static int sort_time(struct directory_entry** entry1, struct directory_entry** entry2);

static bool (*filter)(const char*) = filter_default;
static void (*output)(struct directory_listing*) = output_oneline;
static int (*sort)(struct directory_entry**, struct directory_entry**) = sort_name;

static bool colors = false;
static int deref_mode = LINK_DEREF_MODE_NONE;
static bool print_mode_suffix = false;
static bool unsorted = false;

static inline char get_mode_indicator(mode_t mode) {
    switch (mode & S_IFMT) {
        case S_IFREG:
            return '-';
        case S_IFDIR:
            return 'd';
        case S_IFBLK:
            return 'b';
        case S_IFCHR:
            return 'c';
        case S_IFLNK:
            return 'l';
        default:
            __builtin_unreachable();
    }
}

static inline char get_mode_suffix(mode_t mode) {
    return (S_ISDIR(mode) && print_mode_suffix) ? '/' : '\0';
}

static void add_entry(struct directory_listing* listing, int dirfd, char* entry_name, bool deref_link) {
    struct directory_entry* entry = calloc(1, sizeof(struct directory_entry));
    if (!entry) {
        err(EXIT_FAILURE, "calloc");
    }

    entry->name = strdup(entry_name);
    if (!entry->name) {
        err(EXIT_FAILURE, "strdup");
    }

    if (fstatat(dirfd, entry->name, &entry->stat, deref_link ? 0 : AT_SYMLINK_NOFOLLOW) < 0) {
        warn("failed to stat '%s'", entry->name);
        free(entry->name);
        free(entry);
        return;
    }

    if (S_ISLNK(entry->stat.st_mode) && output == output_long) {
        entry->link_target = malloc(PATH_MAX);
        if (!entry->link_target) {
            err(EXIT_FAILURE, "malloc");
        }

        ssize_t nread = readlinkat(dirfd, entry->name, entry->link_target, PATH_MAX);
        if (nread < 0) {
            warn("readlink: '%s'", entry_name);
            free(entry->name);
            free(entry->link_target);
            free(entry);
            return;
        }

        entry->link_target[nread] = '\0';
    }

    if (listing->entry_count >= listing->capacity) {
        if (listing->capacity == 0) {
            listing->capacity = 4;
        }

        struct directory_entry** new_entries = reallocarray(listing->entries, listing->capacity, 2 * sizeof(struct directory_entry*));
        if (!new_entries) {
            errx(EXIT_FAILURE, "reallocarray");
        }

        listing->entries = new_entries;
        listing->capacity *= 2;
    }

    listing->entries[listing->entry_count++] = entry;
}

static void get_color(mode_t mode, char** pre, char** post) {
    if (!colors) {
        *pre = "";
        *post = "";
        return;
    }

    *post = "\033[0m";

    switch (mode & S_IFMT) {
        case S_IFDIR:
            *pre = "\033[1;34m";
            break;
        case S_IFBLK:
        case S_IFCHR:
            *pre = "\033[1;33m";
            break;
        case S_IFLNK:
            *pre = "\033[1;36m";
            break;
        default:
            *pre = "";
            *post = "";
            break;
    }
}

static bool filter_all(const char* name) {
    (void) name;
    return true;
}

static bool filter_almost_all(const char* name) {
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static bool filter_default(const char* name) {
    return name[0] != '.';
}

static void free_listing(struct directory_listing* listing) {
    for (size_t i = 0; i < listing->entry_count; i++) {
        free(listing->entries[i]->name);

        if (listing->entries[i]->link_target != NULL) {
            free(listing->entries[i]->link_target);
        }

        free(listing->entries[i]);
    }

    listing->entry_count = 0;
}

static void output_columns(struct directory_listing* listing) {
    size_t line_width;

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
        line_width = 80;
    } else {
        line_width = ws.ws_col;
    }

    size_t name_field_length = 0;

    for (size_t i = 0; i < listing->entry_count; i++) {
        size_t length = strlen(listing->entries[i]->name);
        if (length > name_field_length) {
            name_field_length = length;
        }
    }

    size_t max_length = name_field_length;
    if (print_mode_suffix) {
        max_length++;
    }

    size_t columns = (line_width + 1) / (max_length + 1);
    if (columns == 0) {
        columns = 1;
    }

    size_t rows = (listing->entry_count + columns - 1) / columns;

    struct directory_entry* entry;
    for (size_t row = 0; row < rows; row++) {
        for (size_t column = 0; column < columns; column++) {
            size_t index = (column * rows) + row;
            if (index >= (size_t) listing->entry_count) {
                putchar('\n');
                break;
            }

            entry = listing->entries[index];

            char* pre;
            char* post;
            get_color(entry->stat.st_mode, &pre, &post);

            printf("%s%s%s%c%*s%c", pre, entry->name, post, get_mode_suffix(entry->stat.st_mode),
                    (int) (name_field_length - strlen(entry->name)), "", column == columns - 1 ? '\n' : ' ');
        }
    }
}

static void output_long(struct directory_listing* listing) {
    int size_field_length = 0;
    int date_field_length = 0;

    char date_buffer[50];
    time_t now = time(NULL);

    struct directory_entry* entry;

    for (size_t i = 0; i < listing->entry_count; i++) {
        entry = listing->entries[i];

        int length = snprintf(NULL, 0, "%lu", entry->stat.st_size);
        if (length > size_field_length) {
            size_field_length = length;
        }

        time_t time = entry->stat.st_mtim.tv_sec;
        struct tm* tm = localtime(&time);

        length = strftime(date_buffer, sizeof(date_buffer),
                (now - SIX_MONTHS >= time || time > now) ? OLD_TIME_FORMAT : RECENT_TIME_FORMAT, tm);

        if (length > date_field_length) {
            date_field_length = length;
        }
    }

    for (size_t i = 0; i < listing->entry_count; i++) {
        entry = listing->entries[i];

        putchar(get_mode_indicator(entry->stat.st_mode));

        printf(" %*lu ", size_field_length, entry->stat.st_size);

        time_t time = entry->stat.st_mtim.tv_sec;
        struct tm* tm = localtime(&time);

        if (strftime(date_buffer, sizeof(date_buffer),
                    (now - SIX_MONTHS >= time || time > now) ? OLD_TIME_FORMAT : RECENT_TIME_FORMAT, tm) == 0) {
            strcpy(date_buffer, "?");
        }

        printf("%*s ", date_field_length, date_buffer);

        char* pre;
        char* post;
        get_color(entry->stat.st_mode, &pre, &post);

        printf("%s%s%s%c", pre, entry->name, post, get_mode_suffix(entry->stat.st_mode));

        if (entry->link_target) {
            printf(" -> %s", entry->link_target);
        }

        putchar('\n');
    }
}

static void output_oneline(struct directory_listing* listing) {
    struct directory_entry* entry;
    for (size_t i = 0; i < listing->entry_count; i++) {
        entry = listing->entries[i];

        char* pre;
        char* post;
        get_color(entry->stat.st_mode, &pre, &post);

        printf("%s%s%s%c\n", pre, entry->name, post, get_mode_suffix(entry->stat.st_mode));
    }
}

static void print_listing(struct directory_listing* listing) {
    if (!unsorted) {
        qsort(listing->entries, listing->entry_count, sizeof(struct directory_entry*), (int (*)(const void*, const void*)) sort);
    }

    output(listing);
}

static int read_entries(const char* path, struct directory_listing* listing) {
    int fd = open(path, O_DIRECTORY);
    if (fd < 0) {
        warn("cannot access '%s'", path);
        return EXIT_FAILURE;
    }

    DIR* dir = fdopendir(fd);
    if (dir == NULL) {
        warn("failed to open directory");
        close(fd);
        return EXIT_FAILURE;
    }

    errno = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!filter(entry->d_name)) {
            errno = 0;
            continue;
        }

        add_entry(listing, fd, entry->d_name, deref_mode == LINK_DEREF_MODE_ALL);
        errno = 0;
    }

    closedir(dir);
    return EXIT_SUCCESS;
}

static int sort_name(struct directory_entry** entry1, struct directory_entry** entry2) {
    return strcoll((*entry1)->name, (*entry2)->name);
}

static int sort_size(struct directory_entry** entry1, struct directory_entry** entry2) {
    off_t size1 = (*entry1)->stat.st_size;
    off_t size2 = (*entry2)->stat.st_size;

    if (size1 < size2) {
        return 1;
    } else if (size1 > size2) {
        return -1;
    }

    return sort_name(entry1, entry2);
}

static int sort_time(struct directory_entry** entry1, struct directory_entry** entry2) {
    struct timespec ts1 = (*entry1)->stat.st_mtim;
    struct timespec ts2 = (*entry2)->stat.st_mtim;

    if (ts1.tv_sec < ts2.tv_sec) {
        return 1;
    } else if (ts1.tv_sec > ts2.tv_sec) {
        return -1;
    } else if (ts1.tv_nsec < ts2.tv_nsec) {
        return 1;
    } else if (ts1.tv_nsec > ts2.tv_nsec) {
        return -1;
    }

    return sort_name(entry1, entry2);
}

static void usage(void) {
    fprintf(stderr, "usage: ls [-1ACFHLSUaflt] [FILE]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");

    if (isatty(STDOUT_FILENO)) {
        output = output_columns;
        colors = true;
    }

    int c;
    while ((c = getopt(argc, argv, "1ACFHLSUaflt")) != -1) {
        switch (c) {
            case '1':
                output = output_oneline;
                break;
            case 'A':
                if (filter != filter_all) {
                    filter = filter_almost_all;
                }
                break;
            case 'C':
                output = output_columns;
                break;
            case 'F':
                print_mode_suffix = true;
                break;
            case 'H':
                deref_mode = LINK_DEREF_MODE_CLI;
                break;
            case 'L':
                deref_mode = LINK_DEREF_MODE_ALL;
                break;
            case 'S':
                sort = sort_size;
                break;
            case 'U':
                unsorted = true;
                break;
            case 'a':
                filter = filter_all;
                break;
            case 'f':
                filter = filter_all;
                unsorted = true;
                break;
            case 'l':
                output = output_long;
                break;
            case 't':
                sort = sort_time;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    int ret = EXIT_SUCCESS;

    if (argc == 0) {
        struct directory_listing listing;
        ret = read_entries(".", &listing);
        print_listing(&listing);
        free_listing(&listing);
        return ret;
    }

    struct directory_listing listing = {};

    for (int i = 0; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) < 0 || !S_ISDIR(st.st_mode)) {
            add_entry(&listing, AT_FDCWD, argv[i], deref_mode != LINK_DEREF_MODE_NONE);
            argv[i] = NULL;
        }
    }

    print_listing(&listing);
    bool print_newline = listing.entry_count > 0;

    free_listing(&listing);

    bool multiple = (argc >= 2);
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) {
            continue;
        }

        if (multiple) {
            if (print_newline) {
                putchar('\n');
            }

            printf("%s:\n", argv[i]);
            print_newline = true;
        }

        ret |= read_entries(argv[i], &listing);
        print_listing(&listing);
        free_listing(&listing);
    }

    free(listing.entries);
    return ret;
}
