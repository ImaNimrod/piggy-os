#ifndef _KERNEL_UTILS_CMDLINE_H
#define _KERNEL_UTILS_CMDLINE_H

#include <stdbool.h>
#include <stddef.h>

bool cmdline_early_get_klog(void);
char* cmdline_get(const char* key);
void cmdline_parse(void);

#endif /* _KERNEL_UTILS_CMDLINE_H */
