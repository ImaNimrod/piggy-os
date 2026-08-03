#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>

#include "parser.h"

static void node_free(config_node_t* node) {
    while (node) {
        config_node_t* next = node->next;

        free(node->name);

        for (size_t i = 0; i < node->value_count; i++) {
            free(node->values[i]);
        }
        free(node->values);

        node_free(node->children);

        free(node);

        node = next;
    }
}

config_t* config_load_from_file(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    rewind(fp);

    char* buffer = malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t read = fread(buffer, 1, size, fp);
    if (read != (size_t)size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }

    buffer[size] = '\0';

    fclose(fp);

    config_t* config = calloc(1, sizeof(config_t));
    if (!config) {
        return NULL;
    }

    if (!parse_config(config, buffer)) {
        free(buffer);
        free(config);
        return NULL;
    }

    free(buffer);

    return config;
}

void config_free(config_t* config) {
    if (!config) {
        return;
    }

    node_free(config->root.children);
    config->root.children = NULL;

    free(config);
}

config_node_t* config_find(config_node_t* parent, const char* name) {
    if (!parent) {
        return NULL;
    }

    config_node_t* node = parent->children;

    while (node) {
        if (strcmp(node->name, name) == 0) {
            return node;
        }

        node = node->next;
    }

    return NULL;
}

bool config_is_block(config_node_t* node) {
    return node && node->children != NULL;
}

bool config_is_value(config_node_t* node) {
    return node && node->children == NULL;
}

void config_iterator_init(config_iterator_t* iter, config_node_t* parent, const char* name) {
    iter->current = parent ? parent->children : NULL;
    iter->name = name;
}

config_node_t* config_iterator_next(config_iterator_t* iter) {
    while (iter->current) {
        config_node_t* node = iter->current;

        iter->current = node->next;

        if (!strcmp(node->name, iter->name)) {
            return node;
        }
    }

    return NULL;
}

size_t config_value_count(config_node_t* node) {
    if (!node) {
        return 0;
    }

    return node->value_count;
}

int config_value_get_bool(config_node_t* node, size_t index, bool* value) {
    if (!node || !value) {
        return -1;
    }

    if (index >= node->value_count) {
        return -1;
    }

    const char* str = node->values[index];

    if (!strcmp(str, "true") || !strcmp(str, "yes")) {
        *value = true;
        return 0;
    }

    if (!strcmp(str, "false") || !strcmp(str, "no")) {
        *value = false;
        return 0;
    }

    return -1;
}

int config_value_get_int(config_node_t* node, size_t index, long* value) {
    if (!node || !value) {
        return -1;
    }

    if (index >= node->value_count) {
        return -1;
    }

    const char* str = node->values[index];

    char* end_ptr;

    errno = 0;

    long v = strtol(str, &end_ptr, 10);
    if (errno != 0 || end_ptr == str || *end_ptr) {
        return -1;
    }

    *value = v;
    return 0;
}

int config_value_get_string(config_node_t* node, size_t index, const char** value) {
    if (!node || !value) {
        return -1;
    }

    if (index >= node->value_count) {
        return -1;
    }

    *value = node->values[index];

    return 0;
}
