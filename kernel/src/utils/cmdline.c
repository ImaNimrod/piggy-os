#include <limine.h>
#include <mem/slab.h>
#include <utils/cmdline.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/string.h>

static volatile struct limine_kernel_file_request kernel_file_request = {
    .id = LIMINE_KERNEL_FILE_REQUEST,
    .revision = 0
};

static hashmap_t* cmdline_map;
static const char* klog_token = "klog";

bool cmdline_early_get_klog(void) {
    static bool have_klog = false;
    if (have_klog) {
        return true;
    }

    struct limine_kernel_file_response* kernel_file_response = kernel_file_request.response;
    if (kernel_file_response == NULL || kernel_file_response->kernel_file == NULL) {
        return false;
    }

    char* cmdline = kernel_file_response->kernel_file->cmdline;
    if (cmdline == NULL || strlen(cmdline) == 0) {
        return false;
    }

    while (*cmdline != '\0') {
        if (*cmdline == *klog_token && !strncmp(cmdline, klog_token, strlen(klog_token))) {
            have_klog = true;
            return true;
        }
        cmdline++;
    }

    return false;
}

char* cmdline_get(const char* key) {
    if (cmdline_map) {
        return hashmap_get(cmdline_map, key, strlen(key));
    }
    return NULL;
}

void cmdline_parse(void) {
    struct limine_kernel_file_response* kernel_file_response = kernel_file_request.response;

    char* cmdline = kernel_file_response->kernel_file->cmdline;
    if (cmdline == NULL || *cmdline == '\0') {
        return;
    }

    cmdline_map = hashmap_create(20);
    if (cmdline_map == NULL) {
        return;
    }

    size_t bufferlen = strlen(cmdline) + 1;
    char* buffer = kmalloc(bufferlen);

    char* cmdline_ptr = cmdline;
    char* buf_ptr = buffer;

    bool doconvert = true;

    while (*cmdline_ptr) {
        char c = *cmdline_ptr++;
        if (c == ' ' && doconvert) {
            *buf_ptr++ = '\0';
        } else if (c == '"') { 
            doconvert = !doconvert;
            --bufferlen;
        } else {
            *buf_ptr++ = c;
        }
    }

    buffer[bufferlen] = '\0';

    size_t i = 0;
    while (i < bufferlen) {
        char* iter = &buffer[i];
        bool is_pair = false;

        while (*iter != '\0') {
            if (*iter == '=') {
                is_pair = true;
                *iter = '\0';
            }

            iter++;
        }

        size_t keylen = strlen(&buffer[i]);
        if (is_pair) {
            size_t valuelen = strlen(&buffer[i + keylen + 1]);
            char* value = (char*) kmalloc(valuelen) + 1;
            strncpy(value, &buffer[i + keylen + 1], valuelen);

            hashmap_set(cmdline_map, &buffer[i], keylen, value);
            i += valuelen + 1;
        } else {
            char* value = strdup(&buffer[i]);
            hashmap_set(cmdline_map, &buffer[i], keylen, value);
        }

        i += keylen + 1;
    }

    kfree(buffer);
}
