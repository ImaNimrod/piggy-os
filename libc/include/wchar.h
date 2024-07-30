#ifndef _WCHAR_H
#define _WCHAR_H

#include <stddef.h>

typedef int wint_t;

typedef struct {
    unsigned int _state;
    wchar_t _wc;
} mbstate_t;

/* TODO: implement a subset of wchar.h */

#endif /* _WCHAR_H */
