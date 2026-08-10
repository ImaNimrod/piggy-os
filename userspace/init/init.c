#include <piggy/mount.h>
#include <piggy/powerctl.h>

#include <sys/stat.h>
#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <config.h>

#define _PATH_CONFIG "/etc/rc.conf"

enum restart {
    RESTART_NONE = 0,
    RESTART_ALWAYS,
    RESTART_FAILURE,
};

struct service {
    char* name;

    char** argv;
    size_t argc;

    enum restart restart;

    pid_t pid;

    struct service* next;
};

static struct service* service_list;

static inline enum restart restart_from_str(const char* str) {
    if (strcmp(str, "always") == 0) {
        return RESTART_ALWAYS;
    } else if (strcmp(str, "failure") == 0) {
        return RESTART_FAILURE;
    }

    return RESTART_NONE;
}

static bool load_services(config_t* config, struct service** service_list) {
    config_iterator_t iter;

    config_iterator_init(&iter, &config->root, "service");

    config_node_t* node;
    while ((node = config_iterator_next(&iter))) {
        const char* name;
        if (config_value_get_string(node, 0, &name) < 0) {
            fprintf(stderr, "service missing name\n");
            continue;
        }

        struct service* service = calloc(1, sizeof(struct service));
        if (!service) {
            return false;
        }

        service->name = strdup(name);
        if (!service->name) {
            free(service);
            return false;
        }

        config_node_t* command = config_find(node, "command");

        service->argc = config_value_count(command);

        service->argv = calloc(service->argc + 1, sizeof(char*));
        if (!service->argv) {
            free(service->name);
            free(service);
            return false;
        }

        for (size_t i = 0; i < service->argc; i++) {
            const char* value;
            if (config_value_get_string(command, i, &value) < 0) {
                return false;
            }

            service->argv[i] = strdup(value);
            if (!service->argv[i]) {
                return false;
            }
        }

        service->argv[service->argc] = NULL;

        config_node_t* restart = config_find(node, "restart");
        if (restart) {
            const char* value;
            if (config_value_get_string(restart, 0, &value) < 0) {
                return false;
            }

            service->restart = restart_from_str(value);
        }

        service->pid = -1;

        if (!*service_list) {
            *service_list = service;
            continue;
        }

        struct service* iter = *service_list;
        while (iter->next) {
            iter = iter->next;
        }
        iter->next = service;
    }

    return true;
}

static bool mount_pseudofs(void) {
    if (mkdir(_PATH_DEV, 0777) < 0) {
        warn("mkdir");
        return false;
    }

    if (mount(NULL, _PATH_DEV, "devfs") < 0) {
        warn("mount");
        return false;
    }

    return true;
}

static void restart_service(struct service* service, int status) {
    if (service->restart == RESTART_NONE) {
        return;
    }

    bool failed = WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0);

    if (service->restart == RESTART_ALWAYS || (service->restart == RESTART_FAILURE && failed)) {
        pid_t pid = fork();
        if (pid < 0) {
            warn("fork");
            return;
        }

        if (pid == 0) {
            execv(service->argv[0], service->argv);
            err(EXIT_FAILURE, "execv");
        }

        service->pid = pid;

        printf("restarting service %s\n", service->name);
        fflush(stdout);
    }
}

static bool set_hostname(config_t* config) {
    config_node_t* hostname = config_find(&config->root, "hostname");
    if (hostname) {
        const char* value;
        if (config_value_get_string(hostname, 0, &value) < 0) {
            warnx("config: failed to read hostname");
        }

        if (sethostname(value, strlen(value)) < 0) {
            warn("failed to set system hostname");
            return false;
        }

        return true;
    }

    warnx("config: no hostname specified, using kernel default");
    return true;
}

static void signal_handler(int signal);

static bool setup_signals(void) {
    struct sigaction sa;
    sa.sa_flags = 0;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        warn("sigaction(SIGINT)");
        return false;
    }

    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        warn("sigaction(SIGUSR1)");
        return false;
    }

    return true;
}

static bool setup_tempdir(void) {
    if (mkdir(_PATH_TMP, 0777) < 0) {
        if (errno == EEXIST) {
            // TODO: cleanup tmpdir
        } else {
            warn("mkdir");
            return false;
        }
    }

    return true;
}

static void signal_handler(int signal) {
    int op = 0;

    if (signal == SIGINT) {
        op = POWERCTL_REBOOT;
    } else if (signal == SIGUSR1) {
        op = POWERCTL_SHUTDOWN;
    }

    sync();

    powerctl(op);
}

static bool start_services(struct service* service_list) {
    for (struct service* service = service_list; service; service = service->next) {
        pid_t pid = fork();
        if (pid < 0) {
            warn("fork");
            return false;
        }

        if (pid == 0) {
            execv(service->argv[0], service->argv);
            err(EXIT_FAILURE, "execv");
        }

        service->pid = pid;

        printf("starting service %s (pid: %d)\n", service->name, service->pid);
    }

    return true;
}

static int switch_terminal(const char* tty) {
    int rfd = open(tty, O_RDONLY);
    if (rfd < 0) {
        return -1;
    }
    int wfd = open(tty, O_WRONLY);
    if (wfd < 0) {
        return -1;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    if (dup2(rfd, STDIN_FILENO) < 0) {
        return -1;
    }
    if (dup2(wfd, STDOUT_FILENO) < 0) {
        return -1;
    }
    if (dup2(wfd, STDERR_FILENO) < 0) {
        return -1;
    }

    close(rfd);
    close(wfd);

    return 0;
}

int main(void) {
    if (getpid() != 1) {
        errx(EXIT_FAILURE, "init must be run from PID = 1");
    }

    config_t* config = config_load_from_file(_PATH_CONFIG);
    if (!config) {
        errx(EXIT_FAILURE, "failed to load config");
    }

    set_hostname(config);

    if (!load_services(config, &service_list)) {
        errx(EXIT_FAILURE, "failed to load services");
    }

    config_free(config);

    if (!setup_signals()) {
        errx(EXIT_FAILURE, "failed to setup signal handlers");
    }

    if (!mount_pseudofs()) {
        errx(EXIT_FAILURE, "failed to mount pseudo filesystems");
    }

    if (!setup_tempdir()) {
        errx(EXIT_FAILURE, "failed to initialize temporary directory");
    }

    if (!start_services(service_list)) {
        errx(EXIT_FAILURE, "failed to start services");
    }

    fflush(stdout);

    setenv("HOME", "/home", 1);
    setenv("PATH", "/usr/bin", 1);
    setenv("TERM", "linux", 1);

    pid_t pid = fork();
    if (pid < 0) {
        err(EXIT_FAILURE, "fork failed");
    } else if (pid == 0) {
        if (switch_terminal(_PATH_TTY) < 0) {
            err(EXIT_FAILURE, "failed to setup tty for shell");
        }

        chdir("/home");

        char* argv[] = { "/usr/bin/sh", NULL };

        execv(argv[0], argv);
        err(EXIT_FAILURE, "execve");
    }

    for (;;) {
        pid_t pid;
        int status;

        while ((pid = waitpid(-1, &status, 0)) >= 0) {
            for (struct service* service = service_list; service; service = service->next) {
                if (pid == service->pid) {
                    restart_service(service, status);
                }
            }
        }

        if (pid == -1) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ECHILD) {
                pause();
            }
        }
    }

    __builtin_unreachable();
}
