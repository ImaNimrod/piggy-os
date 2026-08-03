#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum config_node_type {
    CONFIG_NODE_BLOCK,
    CONFIG_NODE_VALUE
};

typedef struct config_node {
    char* name;

    enum config_node_type type;

    char** values;
    size_t value_count;

    struct config_node* children;

    struct config_node* next;
} config_node_t;

typedef struct {
    config_node_t root;
} config_t;

typedef struct {
    config_node_t* current;
    const char* name;
} config_iterator_t;

config_t* config_load_from_file(const char* path);
void config_free(config_t* config);

config_node_t* config_find(config_node_t* parent, const char* name);

bool config_is_block(config_node_t* node);
bool config_is_value(config_node_t* node);

void config_iterator_init(config_iterator_t* iter, config_node_t* parent, const char* name);
config_node_t* config_iterator_next(config_iterator_t* iter);

size_t config_value_count(config_node_t* node);

int config_value_get_bool(config_node_t* node, size_t index, bool* value);
int config_value_get_int(config_node_t* node, size_t index, long* value);
int config_value_get_string(config_node_t* node, size_t index, const char** value);

#ifdef __cplusplus
}
#endif

#endif /* _CONFIG_H */
