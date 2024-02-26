#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

#include <limine.h>
#include <stdint.h>

extern volatile struct limine_hhdm_request hhdm_request;

#define KERNEL_HIGH_VMA (hhdm_request.response->offset)

#endif /* _KERNEL_TYPES_H */
