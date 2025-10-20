#include <cpu/asm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>

#include "definitions.h"

static inline void io_wait_400ns(struct ata_channel* channel) {
    inb(channel->control_base);
    inb(channel->control_base);
    inb(channel->control_base);
    inb(channel->control_base);
}

void ata_device_identify(struct ata_channel* channel, bool is_secondary) {
    outb(channel->control_base, 0);
    outb(channel->io_base + REGISTER_DEVICE, 0xe0 | (is_secondary << 4u));
    io_wait_400ns(channel);

    uint8_t status;
    do {
        status = inb(channel->io_base + REGISTER_STATUS);
    } while ((status & (STATUS_DRQ | STATUS_BSY)) & !(status & STATUS_RDY));

    outb(channel->io_base + REGISTER_SECTOR_COUNT, 0);
    outb(channel->io_base + REGISTER_LBA_LOW, 0);
    outb(channel->io_base + REGISTER_LBA_MID, 0);
    outb(channel->io_base + REGISTER_LBA_HIGH, 0);
    outb(channel->io_base + REGISTER_COMMAND, ATA_COMMAND_IDENTIFY_DEVICE);

    status = inb(channel->io_base + REGISTER_STATUS);
    if (status == 0x00 || status == 0xff) { // easy way to tell that no device is connected
        return;
    }

    while (status & STATUS_BSY) {
        status = inb(channel->io_base + REGISTER_STATUS);
    }

    uint8_t lba_mid = inb(channel->io_base + REGISTER_LBA_MID);
    uint8_t lba_high = inb(channel->io_base + REGISTER_LBA_HIGH);

    if ((lba_mid == 0xff) && (lba_high == 0xff)) {
        return;
    }

    ata_device_type_t type;
    if ((lba_mid == 0x00) && (lba_high == 0x00)) {
        type = ATA_DEVICE_TYPE_PATA;
    } else if ((lba_mid == 0x14) && (lba_high == 0xeb)) {
        type = ATA_DEVICE_TYPE_PATAPI;
    } else if ((lba_mid == 0x3c) && (lba_high == 0xc3)) {
        type = ATA_DEVICE_TYPE_SATA;
    } else if ((lba_mid == 0x69) && (lba_high == 0x96)) {
        type = ATA_DEVICE_TYPE_SATAPI;
    } else {
        type = ATA_DEVICE_TYPE_UNKNOWN;
    }

    while (!(status & (STATUS_ERR | STATUS_DRQ | STATUS_DFT | STATUS_RDY))) {
        status = inb(channel->io_base + REGISTER_STATUS);
    }
    if (status & (STATUS_ERR | STATUS_DFT)) {
        return;
    }

    uint16_t identity_buffer[256];
    for (size_t i = 0; i < 256; i++) {
        identity_buffer[i] = inw(channel->io_base + REGISTER_DATA);
    }

    /* if device doesn't support DMA, we don't care */
    if (unlikely(!(identity_buffer[49] & (1 << 8)))) {
        return;
    }

    bool lba48_supported = identity_buffer[83] & (1 << 10);

    size_t sector_count;
    if (lba48_supported) {
        sector_count = identity_buffer[100] | (identity_buffer[101] << 16) | ((uint64_t) identity_buffer[102] << 32) | ((uint64_t) identity_buffer[103] << 48);
    } else {
        sector_count = identity_buffer[60] | (identity_buffer[61] << 16);
    }

    size_t sector_size = 512;
    if ((identity_buffer[106] & (1 << 14)) && !(identity_buffer[106] & (1 << 15))) {
        if (identity_buffer[106] & (1 << 12)) {
            sector_size = 2 * (identity_buffer[117] | (identity_buffer[118] << 16));
        }
    }

    size_t total_size;
    if (__builtin_mul_overflow(sector_count, sector_size, &total_size)) {
        return;
    }

    struct ata_device* device = kmalloc(sizeof(struct ata_device));
    if (unlikely(device == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for ATA device\n");
    }

    device->channel = channel;
    device->is_secondary = is_secondary;
    device->lba48_supported = lba48_supported;

    device->type = type;

    copy_ata_string(device->serial_number, sizeof(device->serial_number), (uint8_t*) &identity_buffer[10]);
    copy_ata_string(device->firmware_revision, sizeof(device->firmware_revision), (uint8_t*) &identity_buffer[23]);
    copy_ata_string(device->model_number, sizeof(device->model_number), (uint8_t*) &identity_buffer[27]);

    device->sector_count = sector_count;
    device->sector_size = sector_size;

    klog("[ata] found %s device (size: %zuGB, block size: %zuB)\n",
         ata_device_type_str(device->type), total_size / 1000000000, sector_size);
}
