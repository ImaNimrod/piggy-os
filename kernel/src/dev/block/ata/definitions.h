#include <dev/block/ata.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATA_ISA_IRQ0 14
#define ATA_ISA_IRQ1 15

#define REGISTER_DATA           0
#define REGISTER_ERROR          1
#define REGISTER_FEATURES       REGISTER_ERROR
#define REGISTER_SECTOR_COUNT   2
#define REGISTER_LBA_LOW        3
#define REGISTER_LBA_MID        4
#define REGISTER_LBA_HIGH       5
#define REGISTER_DEVICE         6
#define REGISTER_STATUS         7
#define REGISTER_COMMAND        REGISTER_STATUS

#define STATUS_ERR (1 << 0)
#define STATUS_DRQ (1 << 3)
#define STATUS_DFT (1 << 5)
#define STATUS_RDY (1 << 6)
#define STATUS_BSY (1 << 7)

#define BUSMASTER_REGISTER_COMMAND  0
#define BUSMASTER_REGISTER_STATUS   2
#define BUSMASTER_REGISTER_PRDT     4

#define BUSMASTER_STATUS_ERROR      (1 << 1)
#define BUSMASTER_STATUS_INTERRUPT  (1 << 2)

struct ata_channel {
    uint16_t io_base;
    uint16_t control_base;
    uint16_t busmaster_base;
    uint8_t irq;
    uintptr_t prdt_paddr;
    uintptr_t dma_area_paddr;
};

struct ata_device {
    struct ata_channel* channel;

    bool is_secondary;
    bool lba48_supported;

    ata_device_type_t type;
    char serial_number[ATA_IDENTIFY_SERIAL_SIZE + 1];
    char firmware_revision[ATA_IDENTIFY_FIRMWARE_SIZE + 1];
    char model_number[ATA_IDENTIFY_MODEL_SIZE + 1];
    size_t sector_count;
    size_t sector_size;
};

void ata_device_identify(struct ata_channel* channel, bool is_secondary);
