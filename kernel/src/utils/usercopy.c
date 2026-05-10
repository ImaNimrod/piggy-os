#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <utils/usercopy.h>

extern int context_call_and_switch(void (*fn)(struct registers* r, void* arg), void* arg, void* stack);

struct memcpy_args {
    void* dest;
    const void* src;
    size_t n;
};

struct memset_args {
    void* dest;
    int c;
    size_t n;
};

struct strlen_args {
    const char* str;
    size_t* ret;
};

static void memcpy_internal(struct registers* r, void* arg) {
    struct memcpy_args* args = arg;

    this_cpu()->scheduler.current_thread->usercopy_registers = r;

    if (this_cpu()->has_smap) {
        stac();
    }

    if (!(args->n & 0x7)) {
        memcpy64(args->dest, args->src, args->n >> 3);
    } else {
        memcpy8(args->dest, args->src, args->n);
    }

    if (this_cpu()->has_smap) {
        clac();
    }

    this_cpu()->scheduler.current_thread->usercopy_registers = NULL;
    r->rax = 0;
}

static void memset_internal(struct registers* r, void* arg) {
    struct memset_args* args = arg;

    this_cpu()->scheduler.current_thread->usercopy_registers = r;

    if (this_cpu()->has_smap) {
        stac();
    }

    if (!(args->n & 0x7)) {
        memset64(args->dest, args->c, args->n >> 3);
    } else {
        memset8(args->dest, args->c, args->n);
    }

    if (this_cpu()->has_smap) {
        clac();
    }

    this_cpu()->scheduler.current_thread->usercopy_registers = NULL;
    r->rax = 0;
}

static void strlen_internal(struct registers* r, void* arg) {
    struct strlen_args* args = arg;

    this_cpu()->scheduler.current_thread->usercopy_registers = r;

    if (this_cpu()->has_smap) {
        stac();
    }

    *args->ret = strlen(args->str);

    if (this_cpu()->has_smap) {
        clac();
    }

    this_cpu()->scheduler.current_thread->usercopy_registers = NULL;
    r->rax = 0;
}

int user_memcpy_from_user(void* restrict dest, const void* restrict usrc, size_t n) {
    if (!IS_USER_ADDRESS(usrc)) {
        return -EFAULT;
    }

    struct memcpy_args args = { dest, usrc, n };
    return context_call_and_switch(memcpy_internal, &args, NULL);
}

int user_memcpy_to_user(void* restrict udest, const void* restrict src, size_t n) {
    if (!IS_USER_ADDRESS(udest)) {
        return -EFAULT;
    }

    struct memcpy_args args = { udest, src, n };
    return context_call_and_switch(memcpy_internal, &args, NULL);
}

int user_memset(void* udest, int c, size_t n) {
    if (!IS_USER_ADDRESS(udest)) {
        return -EFAULT;
    }

    struct memset_args args = { udest, c, n };
    return context_call_and_switch(memset_internal, &args, NULL);
}

int user_strlen(const char* ustr, size_t* ret) {
    if (!IS_USER_ADDRESS(ustr)) {
        return -EFAULT;
    }

    struct strlen_args args = { ustr, ret };
    return context_call_and_switch(strlen_internal, &args, NULL);
}
