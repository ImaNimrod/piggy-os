#ifndef _KERNEL_UTILS_LIST_H
#define _KERNEL_UTILS_LIST_H

#include <stddef.h>

#define SLIST_FOREACH(head, iter, next_field) \
    for ((iter) = (head); (iter) != NULL; (iter) = (iter)->next_field)

#define SLIST_PUSH_FRONT(head, node, next_field) \
do { \
    (node)->next_field = (head); \
    (head) = (node); \
} while (0)

#define SLIST_REMOVE(head, target, next_field) \
do { \
    if ((head) == (target)) { \
        (head) = (head)->next_field; \
    } else { \
        typeof(target) _cur = (head); \
        while (_cur && _cur->next_field != (target)) \
            _cur = _cur->next_field; \
        if (_cur) _cur->next_field = target->next_field; \
    } \
} while (0)

#define DLIST_FOREACH(head, iter, next_field) \
    for ((iter) = (head); (iter) != NULL; (iter) = (iter)->next_field)

#define DLIST_IS_EMPTY(head) ((head) == NULL)

#define DLIST_PUSH_FRONT(head, node, prev_field, next_field) \
do { \
    (node)->prev_field = NULL; \
    (node)->next_field = (head); \
    if ((head) != NULL) \
        (head)->prev_field = (node); \
    (head) = (node); \
} while (0)

#define DLIST_REMOVE(head, node, prev_field, next_field) \
do { \
    if ((node)->prev_field != NULL) \
        (node)->prev_field->next_field = (node)->next_field; \
    else \
        (head) = (node)->next_field; \
 \
    if ((node)->next_field != NULL) \
        (node)->next_field->prev_field = (node)->prev_field; \
 \
    (node)->prev_field = NULL; \
    (node)->next_field = NULL; \
} while (0)

#endif /* _KERNEL_UTILS_LIST_H */
