#include <cpu/asm.h>
#include <cpu/smp.h>
#include <errno.h>
#include <utils/usercopy.h>

// TODO: make these functions actually safe using context switching + GPF / PF detection

int user_memcpy_from_user(void* restrict dest, const void* restrict src, size_t n) {
    if (!IS_USER_ADDRESS(src)) {
        return -EFAULT;
    }

    if (this_cpu()->has_smap) {
        stac();
    }

    if (!(n & 0x7)) {
        memcpy64(dest, src, n >> 3);
    } else {
        memcpy8(dest, src, n);
    }

    if (this_cpu()->has_smap) {
        clac();
    }

    return 0;
}

int user_memcpy_to_user(void* restrict dest, const void* restrict src, size_t n) {
    if (!IS_USER_ADDRESS(dest)) {
        return -EFAULT;
    }

    if (this_cpu()->has_smap) {
        stac();
    }

    if (!(n & 0x7)) {
        memcpy64(dest, src, n >> 3);
    } else {
        memcpy8(dest, src, n);
    }

    if (this_cpu()->has_smap) {
        clac();
    }

    return 0;
}

int user_memset(void* dest, int c, size_t n) {
    if (!IS_USER_ADDRESS(dest)) {
        return -EFAULT;
    }

    if (this_cpu()->has_smap) {
        stac();
    }

    if (!(n & 0x7)) {
        memset64(dest, c, n >> 3);
    } else {
        memset8(dest, c, n);
    }

    if (this_cpu()->has_smap) {
        clac();
    }

    return 0;
}

int user_strlen(const char* str, size_t* ret) {
    if (!IS_USER_ADDRESS(str)) {
        return -EFAULT;
    }

    if (this_cpu()->has_smap) {
        stac();
    }

    *ret = strlen(str);

    if (this_cpu()->has_smap) {
        clac();
    }

    return 0;
}
