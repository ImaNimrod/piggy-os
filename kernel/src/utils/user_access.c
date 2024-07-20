#include <cpu/asm.h>
#include <cpu/percpu.h>
#include <utils/user_access.h>

bool check_user_ptr(const void* ptr) {
    return ((uintptr_t) ptr >= this_cpu()->running_thread->process->code_base &&
        (uintptr_t) ptr < PROCESS_THREAD_STACK_TOP);
}

void* copy_from_user(void* kdest, const void* usrc, size_t size) {
    if (!check_user_ptr(usrc)) {
        return NULL;
    }

    if (this_cpu()->smepsmap_enabled) {
        stac();
    }

    void* ret = memcpy(kdest, usrc, size);

    if (this_cpu()->smepsmap_enabled) {
        clac();
    }

    return ret;
}

void* copy_to_user(void* udest, const void* ksrc, size_t size) {
    if (!check_user_ptr(udest)) {
        return NULL;
    }

    if (this_cpu()->smepsmap_enabled) {
        stac();
    }

    void* ret = memcpy(udest, ksrc, size);

    if (this_cpu()->smepsmap_enabled) {
        clac();
    }

    return ret;
}
