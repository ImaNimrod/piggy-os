#ifndef _KERNEL_IO_PORT_H
#define _KERNEL_IO_PORT_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t data) {
    __asm__ volatile("outb %%al, %%dx" :: "d" (port), "a" (data));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %%dx, %%al" : "=a" (ret) : "d" (port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t data) {
    __asm__ volatile("outw %%ax, %%dx" :: "d" (port), "a" (data));
}

static inline uint16_t inw(uint16_t port){
    uint16_t ret;
    __asm__ volatile("inw %%dx, %%ax" : "=a" (ret) : "d" (port));
    return ret;
} 

/* 32 bits of data */
static inline void outl(uint16_t port, uint32_t data) {
    __asm__ volatile("outl %%eax, %%dx" :: "d" (port), "a" (data));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %%dx, %%eax" : "=a" (ret) : "d" (port));
    return ret;
}

#endif /* _KERNEL_IO_PORT_H */
