#include <limine.h>
#include <mem/slab.h>
#include <stdbool.h>
#include <stddef.h>
#include <utils/cmdline.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

#define CMDLINE_MAX_LEN 1024 

extern struct limine_executable_cmdline_request executable_cmdline_request;

static hashmap_t* cmdline_hashmap;

char* cmdline_get(const char* key) {
    char* value = NULL;

    if (cmdline_hashmap != NULL) {
        hashmap_get(cmdline_hashmap, key, strlen(key), (void**) &value);
    }

    return value;
}

void cmdline_parse(void) {
    struct limine_executable_cmdline_response* executable_cmdline_response = executable_cmdline_request.response;

    char* cmdline = executable_cmdline_response->cmdline;
    if (unlikely(cmdline == NULL || *cmdline == '\0')) {
        return;
    }

    cmdline_hashmap = hashmap_create(20);
    if (unlikely(cmdline_hashmap == NULL)) {
        kpanic(NULL, false, "failed to create kernel command line map");
    }

    char buffer[CMDLINE_MAX_LEN + 1] = {0};
    strncpy(buffer, cmdline, CMDLINE_MAX_LEN);

    size_t buffer_len = MIN(CMDLINE_MAX_LEN, strlen(cmdline));

    char* cmdline_ptr = cmdline;
    char* buf_ptr = buffer;

    bool doconvert = true;

    while (*cmdline_ptr != '\0') {
        char c = *cmdline_ptr++;
        if (c == ' ' && doconvert) {
            *buf_ptr++ = '\0';
        } else if (c == '"') { 
            doconvert = !doconvert;
            buffer_len--;
        } else {
            *buf_ptr++ = c;
        }
    }

    buffer[buffer_len] = '\0';

    size_t i = 0;
    while (i < buffer_len) {
        char* iter = &buffer[i];
        bool is_pair = false;

        while (*iter != '\0') {
            if (*iter == '=') {
                is_pair = true;
                *iter = '\0';
            }

            iter++;
        }

        size_t key_len = strlen(&buffer[i]);

        if (is_pair) {
            size_t value_len = strlen(&buffer[i + key_len + 1]);
            char* value = kmalloc(value_len + 1);
            strncpy(value, &buffer[i + key_len + 1], value_len);

            hashmap_set(cmdline_hashmap, &buffer[i], key_len, value);
            i += value_len + 1;
        } else {
            char* value = strdup(&buffer[i]);
            hashmap_set(cmdline_hashmap, &buffer[i], key_len, value);
        }

        i += key_len + 1;
    }
}
