#ifndef _KERNEL_UTILS_LIST_H
#define _KERNEL_UTILS_LIST_H

#include <stddef.h>

#define SLIST_PUSH_FRONT(head, node) \
do { \
    (node)->next = (head); \
    (head) = (node); \
} while (0)

#define SLIST_PUSH_BACK(head, node) \
do { \
    (node)->next = NULL; \
    if ((head) == NULL) { \
        (head) = (node); \
    } else { \
        typeof(head) _cur = (head); \
        while (_cur->next != NULL) \
            _cur = _cur->next; \
        _cur->next = (node); \
    } \
} while (0)

#define SLIST_REMOVE(head, target) \
do { \
    if ((head) == (target)) { \
        (head) = (head)->next; \
    } else { \
        typeof(target) _cur = (head); \
        while (_cur && _cur->next != (target)) \
            _cur = _cur->next; \
        if (_cur) _cur->next = target->next; \
    } \
} while (0)

#define SLIST_FOREACH(head, iter) \
    for ((iter) = (head); (iter) != NULL; (iter) = (iter)->next)

#endif /* _KERNEL_UTILS_LIST_H */
