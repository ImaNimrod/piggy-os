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
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <config.h>

#define _PATH_CONFIG "/etc/rc.conf"

#define SHUTDOWN_TIMEOUT 5

enum restart {
    RESTART_NONE = 0,
    RESTART_ALWAYS,
    RESTART_FAILURE,
};

enum service_state {
    SERVICE_STOPPED,
    SERVICE_RUNNING,
};

struct service {
    char* name;

    char** argv;
    size_t argc;

    enum restart restart;

    pid_t pid;
    enum service_state state;

    struct service* next;
};

static volatile sig_atomic_t powerctl_op;

static struct service* service_list;
static atomic_bool shutting_down;

static inline enum restart restart_from_str(const char* str) {
    if (strcmp(str, "always") == 0) {
        return RESTART_ALWAYS;
    } else if (strcmp(str, "failure") == 0) {
        return RESTART_FAILURE;
    }

    return RESTART_NONE;
}

static inline bool should_restart(struct service* service, int status) {
    if (service->restart == RESTART_NONE) {
        return false;
    }

    if (service->restart == RESTART_ALWAYS) {
        return true;
    }

    if (service->restart == RESTART_FAILURE) {
        if (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0)) {
            return true;
        }
    }

    return false;
}

bool start_service(struct service* service);

static void exit_service(struct service* service, int status) {
    printf("service %s exited", service->name);

    if (WIFEXITED(status)) {
        printf(" with status %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf(" from signal %d\n", WTERMSIG(status));
    } else {
        putchar('\n');
    }

    service->pid = -1;
    service->state = SERVICE_STOPPED;

    if (!atomic_load_explicit(&shutting_down, memory_order_acquire) && should_restart(service, status)) {
        start_service(service);
    }
}

static struct service* find_service(pid_t pid) {
    for (struct service* service = service_list; service; service = service->next) {
        if (service->pid == pid) {
            return service;
        }
    }

    return NULL;
}

static void kill_services(int signum) {
    for (struct service* service = service_list; service; service = service->next) {
        if (service->pid == SERVICE_RUNNING) {
            if (kill(service->pid, signum) < 0) {
                if (errno != ESRCH) {
                    warn("kill %s", service->name);
                }
            }
        }
    }
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

        config_node_t* disabled = config_find(node, "disabled");
        if (disabled) {
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

static bool redirect_service_stdio(void) {
    int fd = open(_PATH_DEVNULL, O_RDWR);
    if (fd < 0) {
        return false;
    }

    if (dup2(fd, STDIN_FILENO) < 0) {
        goto fail;
    }

    if (dup2(fd, STDOUT_FILENO) < 0) {
        goto fail;
    }

    if (dup2(fd, STDERR_FILENO) < 0) {
        goto fail;
    }

    close(fd);
    return true;

fail:
    close(fd);
    return false;
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

static bool services_running(void) {
    for (struct service* service = service_list; service; service = service->next) {
        if (service->pid >= 0) {
            return true;
        }
    }

    return false;
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

static void shutdown_services(void) {
    atomic_store_explicit(&shutting_down, true, memory_order_relaxed);

    printf("stopping services\n");
    fflush(stdout);

    kill_services(SIGTERM);

    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0) {
        warn("clock_gettime(CLOCK_MONOTONIC)");
        kill_services(SIGKILL);
    } else {
        deadline.tv_sec += SHUTDOWN_TIMEOUT;

        while (services_running()) {
            int status;

            pid_t pid = waitpid(-1, &status, 0);
            if (pid >= 0) {
                struct service* service = find_service(pid);
                if (service) {
                    exit_service(service, status);
                }

                continue;
            }

            if (errno == EINTR) {
            } else if (errno == ECHILD) {
                break;
            } else {
                warn("waitpid");
                break;
            }

            struct timespec now;
            if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
                warn("clock_gettime(CLOCK_MONOTONIC)");
                break;
            }

            if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                break;
            }
        }

        if (services_running()) {
            printf("service shutdown timed out, sending SIGKILL\n");
            fflush(stdout);

            kill_services(SIGKILL);
        }
    }

    while (services_running()) {
        int status;

        pid_t pid = waitpid(-1, &status, 0);
        if (pid >= 0) {
            struct service* service = find_service(pid);
            if (service) {
                exit_service(service, status);
            }

            continue;
        }

        if (errno == EINTR) {
            continue;
        } else if (errno == ECHILD) {
            break;
        }

        warn("waitpid");
        break;
    }
}

static void signal_handler(int signum) {
    if (signum == SIGINT) {
        powerctl_op = POWERCTL_REBOOT;
    } else if (signum == SIGUSR1) {
        powerctl_op = POWERCTL_SHUTDOWN;
    }
}

bool start_service(struct service* service) {
    pid_t pid = fork();
    if (pid < 0) {
        warn("fork");
        return false;
    }

    if (pid == 0) {
        if (!redirect_service_stdio()) {
            _Exit(EXIT_FAILURE);
        }

        execv(service->argv[0], service->argv);
        _Exit(EXIT_FAILURE);
    }

    service->pid = pid;
    service->state = SERVICE_RUNNING;

    printf("started service %s (pid: %d)\n", service->name, pid);
    return true;
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

    struct sigaction sa;
    sa.sa_flags = 0;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        err(EXIT_FAILURE, "sigaction(SIGINT)");
    }
    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        err(EXIT_FAILURE, "sigaction(SIGUSR1)");
    }

    if (!mount_pseudofs()) {
        errx(EXIT_FAILURE, "failed to mount pseudo filesystems");
    }

    if (!setup_tempdir()) {
        errx(EXIT_FAILURE, "failed to initialize temporary directory");
    }

    setenv("HOME", "/home", 1);
    setenv("PATH", "/usr/bin", 1);
    setenv("TERM", "linux", 1);

    printf("starting services...\n");
    fflush(stdout);

    for (struct service* service = service_list; service; service = service->next) {
        if (!start_service(service)) {
            printf("failed to start service %s", service->name);
        }
    }

    for (;;) {
        if (powerctl_op) {
            shutdown_services();

            sync();

            powerctl(powerctl_op);
            __builtin_unreachable();
        }

        int status;

        pid_t pid = waitpid(-1, &status, 0);
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ECHILD) {
                pause();
                continue;
            }

            warn("waitpid");
            continue;
        }

        struct service* service = find_service(pid);
        if (service) {
            exit_service(service, status);
        }
    }

    __builtin_unreachable();
}
