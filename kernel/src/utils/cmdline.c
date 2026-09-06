#include <limine.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

extern struct limine_executable_cmdline_request executable_cmdline_request;

static char* cmdline;

char* cmdline_get(const char* key) {
    if (unlikely(!cmdline)) {
        return NULL;
    }

    size_t key_len = strlen(key);

    const char *p = cmdline;

    while (*p) {
        while (*p == ' ') {
            p++;
        }

        const char* start = p;

        while (*p && *p != ' ') {
            p++;
        }

        size_t len = p - start;

        if (len > key_len && start[key_len] == '=' && memcmp(start, key, key_len) == 0) {
            return (char*) &start[key_len + 1];
        }

        if (len == key_len && memcmp(start, key, key_len) == 0) {
            return (char*) start;
        }
    }

    return NULL;
}

void cmdline_init(void) {
    struct limine_executable_cmdline_response* executable_cmdline_response = executable_cmdline_request.response;

    cmdline = executable_cmdline_response->cmdline;
    if (unlikely(!cmdline || *cmdline == '\0')) {
        return;
    }

    cmdline = strdup(cmdline);
    if (unlikely(!cmdline)) {
        kpanic(NULL, false, "failed to allocate memory for kernel command line");
    }

    klog("[cmdline] kernel command line is '%s'\n", cmdline);
}
