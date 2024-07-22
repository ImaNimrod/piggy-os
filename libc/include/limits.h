#ifndef _LIMITS_H
#define _LIMITS_H

#define _LIBC_LIMITS_H_

#ifndef _GCC_LIMITS_H_
#  include_next <limits.h>
#endif

#undef MB_LEN_MAX
#define MB_LEN_MAX 4

#define ATEXIT_MAX 32
#define SSIZE_MAX LONG_MAX

#endif /* _LIMITS_H */
