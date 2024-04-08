#ifndef _KERNEL_UTILS_MATH_H
#define _KERNEL_UTILS_MATH_H

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

#define DIV_CEIL(VALUE, DIV) (((VALUE) + ((DIV) - 1)) / (DIV))

#define ALIGN_UP(VALUE, ALIGN) (DIV_CEIL((VALUE), (ALIGN)) * ALIGN)
#define ALIGN_DOWN(VALUE, ALIGN) (((VALUE) / (ALIGN)) * (ALIGN))
#define IS_ALIGNED(VALUE, ALIGN) (((VALUE) & ((ALIGN) - 1)) == 0)

#define SIZEOF_ARRAY(XS) (sizeof(XS) / sizeof(XS[0]))

#endif /* _KERNEL_UTILS_MATH_H */
