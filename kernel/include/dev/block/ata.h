#ifndef _KERNEL_DEV_BLOCK_ATA_H
#define _KERNEL_DEV_BLOCK_ATA_H 1

#include <dev/pci.h>
#include <stddef.h>
#include <stdint.h>

#define ATA_COMMAND_READ_DMA_EXT    0x25
#define ATA_COMMAND_WRITE_DMA_EXT   0x35
#define ATA_COMMAND_FLUSH_CACHE     0xe7
#define ATA_COMMAND_FLUSH_CACHE_EXT 0xea
#define ATA_COMMAND_IDENTIFY_DEVICE 0xec

#define ATA_IDENTIFY_SERIAL_SIZE     20
#define ATA_IDENTIFY_FIRMWARE_SIZE   8
#define ATA_IDENTIFY_MODEL_SIZE      40

typedef enum {
    ATA_DEVICE_TYPE_PATA,
    ATA_DEVICE_TYPE_PATAPI,
    ATA_DEVICE_TYPE_SATA,
    ATA_DEVICE_TYPE_SATAPI,
    ATA_DEVICE_TYPE_UNKNOWN,
} ata_device_type_t;

extern struct pci_driver ata_driver;

static inline const char* ata_device_type_str(ata_device_type_t type) {
    switch (type) {
        case ATA_DEVICE_TYPE_PATA: return "PATA";
        case ATA_DEVICE_TYPE_PATAPI: return "PATAPI";
        case ATA_DEVICE_TYPE_SATA: return "SATA";
        case ATA_DEVICE_TYPE_SATAPI: return "SATAPI";
        default: return "unknown";
    }
}

static inline void copy_ata_string(char* buf, size_t len, const uint8_t* ata_string) {
    for (size_t i = 0; i < len - 1; i +=2) {
        buf[i] = ata_string[i + 1];
        buf[i + 1] = ata_string[i];
    }
    buf[len] = '\0';
}

#endif /* _KERNEL_DEV_BLOCK_ATA_H */
