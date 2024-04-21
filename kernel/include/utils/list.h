#ifndef _KERNEL_UTILS_LIST_H
#define _KERNEL_UTILS_LIST_H

#include <stddef.h>

#define LIST_HEAD(type) struct { \
    type* last;  \
    type* first; \
}

#define LIST_ENTRY(type) struct { \
    type* next; \
    type* prev; \
}

#define LIST_INIT(head) do { \
    (head)->first = NULL; \
    (head)->last = NULL; \
} while (0)

#define LIST_ADD_FRONT(head, el, name) do { \
    if ((head)->first == NULL) { \
        (head)->first = (el); \
        (head)->last = (el); \
        (el)->name.next = NULL; \
        (el)->name.prev = NULL; \
    } else { \
        (head)->first->name.prev = (el); \
        (el)->name.next = (head)->first; \
        (el)->name.prev = NULL; \
        (head)->first = (el); \
    } \
} while (0)

#define LIST_ADD_BACK(head, el, name) do { \
    if ((head)->first == NULL) { \
        (head)->first = (el); \
        (head)->last = (el); \
        (el)->name.next = NULL; \
        (el)->name.prev = NULL; \
    } else { \
        (head)->last->name.next = (el); \
        (el)->name.next = NULL; \
        (el)->name.prev = (head)->last; \
        (head)->last = (el); \
    } \
} while (0)

#define LIST_REMOVE(head, el, name) do { \
    if ((el) == (head)->first) { \
        if ((el) == (head)->last) { \
            (head)->first = NULL; \
            (head)->last = NULL; \
        } else { \
            (el)->name.next->name.prev = NULL; \
            (head)->first = (el)->name.next; \
            (el)->name.next = NULL; \
        } \
    } else if ((el) == (head)->last) { \
        (el)->name.prev->name.next = NULL; \
        (head)->last = (el)->name.prev; \
        (el)->name.prev = NULL; \
    } else { \
        (el)->name.next->name.prev = (el)->name.prev; \
        (el)->name.prev->name.next = (el)->name.next; \
        (el)->name.next = NULL; \
        (el)->name.prev = NULL; \
    } \
} while (0)

#define LIST_EMPTY(head) ((head)->first == NULL)
#define LIST_FOREACH(var, head, name) for ((var) = ((head)->first); (var); (var) = ((var)->name.next))

#endif /* _KERNEL_UTILS_LIST_H */
