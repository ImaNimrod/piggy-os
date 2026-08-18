#include <piggy/procctl.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct process {
    struct procctl_status status;
};

struct process_list {
    size_t capacity;
    size_t count;
    struct process* entries;
};

static inline const char* state_name(int state) {
    switch (state) {
        case PROCCTL_STATE_RUNNING:
            return "RUNNING";
        case PROCCTL_STATE_ZOMBIE:
            return "ZOMBIE";
        default:
            return "?";
    }
}

static void add_entry(struct process_list* list, const struct procctl_status* status) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2 : 32;

        struct process* new_entries = reallocarray(list->entries, new_capacity, sizeof(struct process));
        if (!new_entries) {
            errx(EXIT_FAILURE, "reallocarray");
        }

        list->entries = new_entries;
        list->capacity = new_capacity;
    }

    list->entries[list->count++].status = *status;
}

static const struct process* find_entry(const struct process_list* list, pid_t pid) {
    for (size_t i = 0; i < list->count; i++) {
        if (list->entries[i].status.pid == pid) {
            return &list->entries[i];
        }
    }

    return NULL;
}

static void free_entries(struct process_list* list) {
    free(list->entries);

    list->entries = NULL;
    list->count = list->capacity = 0;
}

static void print_children(const struct process_list* list, pid_t parent_pid, const char* prefix) {
    size_t child_count = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (list->entries[i].status.ppid == parent_pid) {
            child_count++;
        }
    }

    size_t child_index = 0;

    for (size_t i = 0; i < list->count; i++) {
        const struct procctl_status* p = &list->entries[i].status;
        if (p->ppid != parent_pid) {
            continue;
        }

        bool last = ++child_index == child_count;

        printf("%s%s %d  %s\n", prefix, last ? "└──" : "├──", p->pid, p->name);

        char child_prefix[1024];

        snprintf(child_prefix, sizeof(child_prefix), "%s%s",
                prefix, last ? "    " : "│   ");

        print_children(list, p->pid, child_prefix);
    }
}

static void print_normal(const struct process_list* list) {
    printf("%-6s %-6s %-6s %-8s %s\n",
            "PID", "PPID", "PGID", "STATE", "NAME");

    for (size_t i = 0; i < list->count; i++) {
        const struct procctl_status* p = &list->entries[i].status;

        printf("%-6d %-6d %-6d %-8s %s\n",
                p->pid, p->ppid, p->pgid, state_name(p->state), p->name);
    }
}

static void print_tree(const struct process_list* list, pid_t root_pid) {
    if (root_pid != PROCCTL_PID_END) {
        const struct process* root = find_entry(list, root_pid);
        if (!root) {
            warnx("process %d not found", root_pid);
            return;
        }

        printf("%d %s\n", root->status.pid, root->status.name);

        print_children(list, root->status.pid, "");
        return;
    }

    for (size_t i = 0; i < list->count; i++) {
        const struct procctl_status* p = &list->entries[i].status;
        if (p->ppid != 0 && find_entry(list, p->ppid)) {
            continue;
        }

        printf("%d %s\n", p->pid, p->name);
        print_children(list, p->pid, "");
    }
}

static void usage(void) {
    fprintf(stderr, "usage: ps [-t] [-p PID]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool have_pid = false;
    bool tree = false;

    pid_t selected_pid = PROCCTL_PID_END;

    char* end_ptr;

    int c;
    while ((c = getopt(argc, argv, "p:t")) != -1) {
        switch (c) {
            case 'p':
                errno = 0;

                unsigned long value = strtoul(optarg, &end_ptr, 10);
                if (errno != 0 || value > INT_MAX || optarg == end_ptr || *end_ptr) {
                    warnx("invalid PID: '%s'", optarg);
                    usage();
                }

                selected_pid = (pid_t) value;
                have_pid = true;
                break;
            case 't':
                tree = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc >= 1) {
        warnx("extra operands provided");
        usage();
    }

    if (have_pid && !tree) {
        struct procctl_status status;

        size_t len = sizeof(status);

        if (procctl(selected_pid, PROCCTL_STATUS, &status, &len) < 0) {
            if (errno = ESRCH) {
                errx(EXIT_FAILURE, "process %d not found", selected_pid);
            } else {
                err(EXIT_FAILURE, "procctl(PROCCTL_STATUS)");
            }
        }

        printf("%-6s %-6s %-6s %-8s %s\n",
                "PID", "PPID", "PGID", "STATE", "NAME");

        printf("%-6d %-6d %-6d %-8s %s\n",
                status.pid, status.ppid, status.pgid, state_name(status.state), status.name);

        return EXIT_SUCCESS;
    }

    struct process_list list = {};

    pid_t pid = 0;

    for (;;) {
        size_t len = sizeof(pid);

        int ret = procctl(pid, PROCCTL_NEXTPID, &pid, &len);
        if (ret < 0) {
            free_entries(&list);
            err(EXIT_FAILURE, "procctl(PROCCTL_NEXTPID)");
        }

        if (pid == PROCCTL_PID_END) {
            break;
        }

        struct procctl_status status;

        len = sizeof(status);

        if (procctl(pid, PROCCTL_STATUS, &status, &len) < 0) {
            continue;
        }

        add_entry(&list, &status);
    }

    if (tree) {
        print_tree(&list, selected_pid);
    } else {
        print_normal(&list);
    }

    free_entries(&list);
    return EXIT_SUCCESS;
}
