#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_PAGER_CMD   "/usr/bin/more"
#define ROFF_CMD            "/usr/bin/roff"

#define SIZEOF_ARRAY(xs) (sizeof((xs)) / sizeof((xs)[0]))

static const char* SECTIONS[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static inline char* build_path(const char* section, const char* page) {
    char* path;
    if (asprintf(&path, "%s/man%s/%s.%s", _PATH_MAN, section, page, section) < 0) {
        err(EXIT_FAILURE, "asprintf");
    }

    return path;
}

static inline bool check_file(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return true;
    }

    return false;
}

static char* find_page(const char* section, const char* page) {
    if (section) {
        char* path = build_path(section, page);
        if (check_file(path)) {
            return path;
        }

        free(path);
        return NULL;
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(SECTIONS); i++) {
        char* path = build_path(SECTIONS[i], page);
        if (check_file(path)) {
            return path;
        }

        free(path);
    }

    return NULL;
}

static const char* get_pager(void) {
    const char* pager = getenv("MANPAGER");
    if (pager && *pager) {
        return pager;
    }

    pager = getenv("PAGER");
    if (pager  && *pager) {
        return pager;
    }

    return DEFAULT_PAGER_CMD;
}

static int run_pipeline(const char* file) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        err(EXIT_FAILURE, "pipe");
    }

    pid_t roff_pid = fork();
    if (roff_pid < 0) {
        err(EXIT_FAILURE, "fork");
    }

    if (roff_pid == 0) {
        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            err(EXIT_FAILURE, "dup2");
        }

        close(pipefd[1]);

        char* roff_cmd[] = { ROFF_CMD, (char*) file, NULL };
        execvp(roff_cmd[0], roff_cmd);
        err(EXIT_FAILURE, "execvp(roff)");
    }

    pid_t pager_pid = fork();
    if (pager_pid < 0) {
        err(EXIT_FAILURE, "fork");
    }

    if (pager_pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        char* pager_cmd[] = { "sh", "-c", (char*) get_pager(), NULL };
        execvp(pager_cmd[0], pager_cmd);
        err(EXIT_FAILURE, "execvp(pager)");
    }

    close(pipefd[0]);
    close(pipefd[1]);

    int roff_status = 0;
    int pager_status = 0;

    waitpid(roff_pid, &roff_status, 0);
    waitpid(pager_pid, &pager_status, 0);

    if (WIFEXITED(pager_status)) {
        int code = WEXITSTATUS(pager_status);
        if (code != 0) {
            return code;
        }
    } else if (WIFSIGNALED(pager_status)) {
        return 128 + WTERMSIG(pager_status);
    }

    if (WIFEXITED(roff_status)) {
        return WEXITSTATUS(roff_status);
    } else if (WIFSIGNALED(roff_status)) {
        return 128 + WTERMSIG(roff_status);
    }

    return EXIT_FAILURE;
}

static void usage(void) {
    fprintf(stderr, "usage: man [SECTION] PAGE\n"
                    "       man -l FILE\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool local_mode = false;

    int c;
    while ((c = getopt(argc, argv, "l")) != -1) {
        switch (c) {
            case 'l':
                local_mode = true;
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
    } else if (argc > (local_mode ? 1 : 2)) {
        warnx("extra operands provided");
        usage();
    }

    char* path = NULL;
    if (local_mode) {
        if (!check_file(argv[0])) {
            warn("%s", argv[0]);
            return EXIT_FAILURE;
        }

        path = strdup(argv[0]);
        if (!path) {
            err(EXIT_FAILURE, "strdup");
        }
    } else {
        const char* page = NULL;
        const char* section = NULL;

        if (argc == 1) {
            page = argv[0];
        } else {
            section = argv[0];
            page = argv[1];
        }

        path = find_page(section, page);
        if (!path) {
            if (section != NULL) {
                warnx("no manual entry for %s in section %s", page, section);
            } else {
                warnx("no manual entry for %s", page);
            }

            return EXIT_FAILURE;
        }
    }

    int ret = run_pipeline(path);

    free(path);
    return ret;
}
