#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/block/ata.h>
#include <dev/block/partition.h>
#include <dev/hpet.h>
#include <dev/ioapic.h>
#include <dev/lapic.h>
#include <errno.h>
#include <fs/devfs.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/sched.h>
#include <sys/time.h>
#include <types.h>
#include <utils/log.h>
#include <utils/panic.h>
#include <utils/spinlock.h>
#include <utils/string.h>

#define ATA_ISA_IRQ0 14
#define ATA_ISA_IRQ1 15

#define ATA_REGISTER_DATA               0
#define ATA_REGISTER_ERROR              1
#define ATA_REGISTER_FEATURES           ATA_REGISTER_ERROR
#define ATA_REGISTER_SECTOR_COUNT       2
#define ATA_REGISTER_LBA_LOW            3
#define ATA_REGISTER_LBA_MID            4
#define ATA_REGISTER_LBA_HIGH           5
#define ATA_REGISTER_DEVICE             6
#define ATA_REGISTER_STATUS             7
#define ATA_REGISTER_COMMAND            ATA_REGISTER_STATUS

#define ATA_STATUS_ERROR                (1 << 0)
#define ATA_STATUS_DATA_REQUEST_READY   (1 << 3)
#define ATA_STATUS_DEVICE_SEEK_COMPLETE (1 << 4)
#define ATA_STATUS_DEVICE_FAULT         (1 << 5)
#define ATA_STATUS_READY                (1 << 6)
#define ATA_STATUS_BUSY                 (1 << 7)

#define ATA_COMMAND_FLUSH_CACHE         0xe7
#define ATA_COMMAND_IDENTIFY_DEVICE     0xec
#define ATA_COMMAND_READ_SECTORS        0x20
#define ATA_COMMAND_READ_SECTORS_EXT    0x24
#define ATA_COMMAND_WRITE_SECTORS       0x30
#define ATA_COMMAND_WRITE_SECTORS_EXT   0x34
#define ATA_COMMAND_READ_DMA            0xc8
#define ATA_COMMAND_READ_DMA_EXT        0x25
#define ATA_COMMAND_WRITE_DMA           0xca
#define ATA_COMMAND_WRITE_DMA_EXT       0x35

#define ATA_CONTROL_SOFTWARE_RESET      0x04

#define ATA_BUSMASTER_REGISTER_COMMAND  0x00
#define ATA_BUSMASTER_REGISTER_STATUS   0x02
#define ATA_BUSMASTER_REGISTER_PRDT     0x04

#define ATA_BUSMASTER_STATUS_ERROR      (1 << 1)
#define ATA_BUSMASTER_STATUS_INTERRUPT  (1 << 2)

#define ATA_BUSMASTER_COMMAND_START     0x01
#define ATA_BUSMASTER_COMMAND_READ      0x08

#define ATA_SERIAL_SIZE     20
#define ATA_FIRMWARE_SIZE   8
#define ATA_MODEL_SIZE      40

typedef enum {
    ATA_DEVICE_TYPE_PATA = 0,
    ATA_DEVICE_TYPE_SATA,
    ATA_DEVICE_TYPE_PATAPI,
    ATA_DEVICE_TYPE_SATAPI,
    ATA_DEVICE_TYPE_UNKNOWN,
} ata_device_type_t;

struct ata_channel {
    uint16_t io_base;
    uint16_t control_base;
    uint16_t busmaster_base;
    uint8_t irq;
    uintptr_t prdt_paddr;
    uintptr_t dma_area_paddr;
    bool awaiting_irq;
    bool dma_in_progress;
    bool error;
    spinlock_t lock;
};

struct ata_device {
    struct ata_channel* channel;
    bool is_secondary;
    bool dma_supported;
    bool lba48_supported;
    ata_device_type_t type;
    size_t sector_count;
    size_t sector_size;
    char serial_number[ATA_SERIAL_SIZE + 1];
    char firmware_revision[ATA_FIRMWARE_SIZE + 1];
    char model_number[ATA_MODEL_SIZE + 1];
};

static const char* ata_device_type_names[] = {
    [ATA_DEVICE_TYPE_PATA] = "PATA",
    [ATA_DEVICE_TYPE_SATA] = "SATA",
    [ATA_DEVICE_TYPE_PATAPI] = "PATAPI",
    [ATA_DEVICE_TYPE_SATAPI] = "SATAPI",
    [ATA_DEVICE_TYPE_UNKNOWN] = "UNKNOWN",
};

static ssize_t ata_read(struct vfs_node* node, void* buf, off_t offset, size_t count, int flags);
static ssize_t ata_write(struct vfs_node* node, const void* buf, off_t offset, size_t count, int flags);
static int ata_sync(struct vfs_node* node);

static size_t ata_device_count = 0;

static inline void copy_ata_string(char* buf, size_t len, uint16_t* ata_string) {
    uint8_t* ata_string_u8 = (uint8_t*) ata_string;

    for (size_t i = 0; i < len - 1; i +=2) {
        buf[i] = ata_string_u8[i + 1];
        buf[i + 1] = ata_string_u8[i];
    }
    buf[len] = '\0';
}

static inline void io_wait_400ns(struct ata_channel* channel) {
    inb(channel->control_base);
    inb(channel->control_base);
    inb(channel->control_base);
    inb(channel->control_base);
}

static inline void software_reset_channel(struct ata_channel* channel) {
    outb(channel->control_base, ATA_CONTROL_SOFTWARE_RESET);
    hpet_sleep_ns(5000); // 5 microseconds
    outb(channel->control_base, 0);

    uint8_t status;
    do {
        status = inb(channel->io_base + ATA_REGISTER_STATUS);
    } while (status & (ATA_STATUS_DATA_REQUEST_READY | ATA_STATUS_BUSY));
}

static void ata_channel_identify_device(struct ata_channel* channel, bool is_secondary) {
    outb(channel->control_base, 0);
    outb(channel->io_base + ATA_REGISTER_DEVICE, 0xe0 | (is_secondary << 4u));
    io_wait_400ns(channel);

    uint8_t status;
    do {
        status = inb(channel->io_base + ATA_REGISTER_STATUS);
    } while (status & (ATA_STATUS_DATA_REQUEST_READY | ATA_STATUS_BUSY));

    /* the spec requires that sector count and LBA are zeroed before running IDENTIFY_DEVICE command*/
    outb(channel->io_base + ATA_REGISTER_SECTOR_COUNT, 0);
    outb(channel->io_base + ATA_REGISTER_LBA_LOW, 0);
    outb(channel->io_base + ATA_REGISTER_LBA_MID, 0);
    outb(channel->io_base + ATA_REGISTER_LBA_HIGH, 0);

    outb(channel->io_base + ATA_REGISTER_COMMAND, ATA_COMMAND_IDENTIFY_DEVICE);

    status = inb(channel->io_base + ATA_REGISTER_STATUS);
    if (status == 0 || status == 0xff) {
        return;
    }
    while (status & ATA_STATUS_BUSY) {
        status = inb(channel->io_base + ATA_REGISTER_STATUS);
    }
    while (!(status & (ATA_STATUS_ERROR | ATA_STATUS_DATA_REQUEST_READY | ATA_STATUS_DEVICE_FAULT | ATA_STATUS_READY))) {
        status = inb(channel->io_base + ATA_REGISTER_STATUS);
    }
    if (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) {
        return;
    }

    /* read device indentification */
    uint16_t data[256];
    for (size_t i = 0; i < 256; i++) {
        data[i] = inw(channel->io_base + ATA_REGISTER_DATA);
    }

    if (data[0] & (1 << 15)) {
        return;
    }

    uint8_t lba_mid = inb(channel->io_base + ATA_REGISTER_LBA_MID);
    uint8_t lba_high = inb(channel->io_base + ATA_REGISTER_LBA_HIGH);

    if ((lba_mid == 0xff) && (lba_high == 0xff)) {
        return;
    }

    bool dma_supported = data[49] & (1 << 8);
    bool lba48_supported = data[83] & (1 << 10);

    size_t sector_count;
    if (lba48_supported) {
        sector_count = data[100] | (data[101] << 16) | ((uint64_t) data[102] << 32) | ((uint64_t) data[103] << 48);
    } else {
        sector_count = data[60] | (data[61] << 16);
    }

    size_t sector_size = 512;
    if ((data[106] & (1 << 14)) && !(data[106] & (1 << 15))) {
        if (data[106] & (1 << 12)) {
            sector_size = 2 * (data[117] | (data[118] << 16));
        }
    }

    size_t total_size;
    if (__builtin_mul_overflow(sector_count, sector_size, &total_size)) {
        return;
    }

    struct ata_device* device = kmalloc(sizeof(struct ata_device));
    if (device == NULL) {
        kpanic(NULL, false, "failed to allocate ata_device structure");
    }

    device->channel = channel;
    device->is_secondary = is_secondary;
    device->dma_supported = dma_supported;
    device->lba48_supported = lba48_supported;

    device->sector_count = sector_count;
    device->sector_size = sector_size;

    if ((lba_mid == 0x00) && (lba_high == 0x00)) {
        device->type = ATA_DEVICE_TYPE_PATA;
    } else if ((lba_mid == 0x3c) && (lba_high == 0xc3)) {
        device->type = ATA_DEVICE_TYPE_SATA;
    } else if ((lba_mid == 0x14) && (lba_high == 0xeb)) {
        device->type = ATA_DEVICE_TYPE_PATAPI;
    } else if ((lba_mid == 0x69) && (lba_high == 0x96)) {
        device->type = ATA_DEVICE_TYPE_SATAPI;
    } else {
        device->type = ATA_DEVICE_TYPE_UNKNOWN;
    }

    copy_ata_string(device->serial_number, sizeof(device->serial_number), &data[10]);
    copy_ata_string(device->firmware_revision, sizeof(device->firmware_revision), &data[23]);
    copy_ata_string(device->model_number, sizeof(device->model_number), &data[27]);

    char ata_node_name[10];
    snprintf(ata_node_name, sizeof(ata_node_name), "ata%u", ata_device_count);

    struct vfs_node* ata_node = devfs_create_device(strdup(ata_node_name));
    if (ata_node == NULL) {
        kpanic(NULL, false, "failed to create device node for ata device #%u in devfs", ata_device_count);
    }

    ata_node->stat = (struct stat) {
        .st_dev = makedev(0, 1),
        .st_mode = S_IFBLK,
        .st_rdev = makedev(ATA_MAJ, ata_device_count++),
        .st_size = sector_count * sector_size,
        .st_blksize = sector_size,
        .st_blocks = sector_count,
        .st_atim = time_realtime,
        .st_mtim = time_realtime,
        .st_ctim = time_realtime,
    };

    ata_node->private = device;

    ata_node->read = ata_read;
    ata_node->write = ata_write;
    ata_node->sync = ata_sync;

    klog("[ata] %s device detected of size %uGB with %uB sectors (%s)\n",
            ata_device_type_names[device->type], total_size >> 30, sector_size, dma_supported ? "DMA mode" : "PIO mode");

    partition_scan(ata_node);
}

static bool ata_channel_finish_dma(struct ata_channel* channel) {
    if (!channel->dma_in_progress) {
        return true;
    }

    while (channel->awaiting_irq) {
        sched_yield();
    }

    outb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_COMMAND, 0);
    channel->dma_in_progress = false;

    if (channel->error) {
        uint8_t value = inb(channel->io_base + ATA_REGISTER_ERROR);
        klog("[ata] ATA error 0x%02x\n", value);
        return false;
    }

    return true;
}

static bool ata_channel_set_sectors(struct ata_channel* channel, size_t sector_count, uint64_t lba, bool is_secondary) {
    bool ret = false;
    if (lba > 0xfffffff || sector_count > 256) {
        uint16_t count = sector_count == 0xffff ? 0 : sector_count;

        outb(channel->io_base + ATA_REGISTER_DEVICE, 0xe0 | (is_secondary << 4u));
        io_wait_400ns(channel);

        outb(channel->io_base + ATA_REGISTER_SECTOR_COUNT, (count >> 8) & 0xff);
        outb(channel->io_base + ATA_REGISTER_LBA_LOW, (lba >> 24) & 0xff);
        outb(channel->io_base + ATA_REGISTER_LBA_MID, (lba >> 32) & 0xff);
        outb(channel->io_base + ATA_REGISTER_LBA_HIGH, (lba >> 40) & 0xff);
        outb(channel->io_base + ATA_REGISTER_SECTOR_COUNT, count & 0xff);
        outb(channel->io_base + ATA_REGISTER_LBA_LOW, lba & 0xff);
        outb(channel->io_base + ATA_REGISTER_LBA_MID, (lba >> 8) & 0xff);
        outb(channel->io_base + ATA_REGISTER_LBA_HIGH, (lba >> 16) & 0xff);

        ret = true;
    } else {
        uint8_t count = sector_count == 256 ? 0 : sector_count;

        outb(channel->control_base, 0);
        outb(channel->io_base + ATA_REGISTER_DEVICE, 0xe0 | (is_secondary << 4u) | ((lba >> 24) & 0x0f));
        io_wait_400ns(channel);

        outb(channel->io_base + ATA_REGISTER_SECTOR_COUNT, count);
        outb(channel->io_base + ATA_REGISTER_LBA_LOW, lba & 0xff);
        outb(channel->io_base + ATA_REGISTER_LBA_MID, (lba >> 8) & 0xff);
        outb(channel->io_base + ATA_REGISTER_LBA_HIGH, (lba >> 16) & 0xff);
    }

    uint8_t status;
    do {
        status = inb(channel->io_base + ATA_REGISTER_STATUS);
    } while ((status & ATA_STATUS_BUSY) && !(status & ATA_STATUS_READY));

    return ret;
}

static bool ata_device_flush_cache(struct ata_device* device) {
    bool ret = false;

    struct ata_channel* channel = device->channel;
    spinlock_acquire(&channel->lock);

    if (device->dma_supported) {
        if (!ata_channel_finish_dma(channel)) {
            goto end;
        }
    }

    outb(channel->control_base, 0);
    outb(channel->io_base + ATA_REGISTER_DEVICE, 0xe0 | (device->is_secondary << 4u));
    io_wait_400ns(channel);

    outb(channel->io_base + ATA_REGISTER_COMMAND, ATA_COMMAND_FLUSH_CACHE);

    uint8_t status;
    do {
        status = inb(channel->io_base + ATA_REGISTER_STATUS);
    } while (status & ATA_STATUS_BUSY);

    if (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) {
        goto end;
    }

    ret = true;
end:
    spinlock_release(&channel->lock);
    return ret;
}

static bool ata_device_read_dma(struct ata_device* device, uint8_t* buf, size_t sector_count, uint64_t lba) {
    bool ret = false;

    struct ata_channel* channel = device->channel;
    spinlock_acquire(&channel->lock);

    if (!ata_channel_finish_dma(channel)) {
        goto end;
    }

    bool need_lba48 = ata_channel_set_sectors(channel, sector_count, lba, device->is_secondary);

    uint32_t* prd = (uint32_t*) (channel->prdt_paddr + HIGH_VMA);
    prd[0] = channel->dma_area_paddr;
    prd[1] = (sector_count * device->sector_size) | (1U << 31);
    outl(channel->busmaster_base + ATA_BUSMASTER_REGISTER_PRDT, channel->prdt_paddr);

    outb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_STATUS,
            inb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_STATUS) |  ATA_BUSMASTER_STATUS_ERROR | ATA_BUSMASTER_STATUS_INTERRUPT);
    outb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_COMMAND, ATA_BUSMASTER_COMMAND_READ);

    uint8_t status;
    do {
        status = inb(channel->io_base + ATA_REGISTER_STATUS);
    } while (status & ATA_STATUS_BUSY);

    outb(channel->io_base + ATA_REGISTER_COMMAND, need_lba48 ? ATA_COMMAND_READ_DMA_EXT : ATA_COMMAND_READ_DMA);
    io_wait_400ns(channel);

    channel->awaiting_irq = true;
    channel->dma_in_progress = true;
    channel->error = false;

    outb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_COMMAND, ATA_BUSMASTER_COMMAND_START | ATA_BUSMASTER_COMMAND_READ);
    if (!ata_channel_finish_dma(channel)) {
        goto end;
    }

    memcpy(buf, (void*) (channel->dma_area_paddr + HIGH_VMA), device->sector_size * sector_count);

    outb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_STATUS,
            inb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_STATUS) |  ATA_BUSMASTER_STATUS_ERROR | ATA_BUSMASTER_STATUS_INTERRUPT);

    ret = true;
end:
    spinlock_release(&channel->lock);
    return ret;
}

static bool ata_device_write_dma(struct ata_device* device, const uint8_t* buf, size_t sector_count, uint64_t lba) {
    bool ret = false;

    struct ata_channel* channel = device->channel;
    spinlock_acquire(&channel->lock);

    if (!ata_channel_finish_dma(channel)) {
        goto end;
    }

    bool need_lba48 = ata_channel_set_sectors(channel, sector_count, lba, device->is_secondary);
    memcpy((void*) (channel->dma_area_paddr + HIGH_VMA), buf, device->sector_size * sector_count);

    uint32_t* prd = (uint32_t*) (channel->prdt_paddr + HIGH_VMA);
    prd[0] = channel->dma_area_paddr;
    prd[1] = (sector_count * device->sector_size) | (1U << 31);
    outl(channel->busmaster_base + ATA_BUSMASTER_REGISTER_PRDT, channel->prdt_paddr);

    outb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_STATUS,
            inb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_STATUS) |  ATA_BUSMASTER_STATUS_ERROR | ATA_BUSMASTER_STATUS_INTERRUPT);
    outb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_COMMAND, 0);

    outb(channel->io_base + ATA_REGISTER_COMMAND, need_lba48 ? ATA_COMMAND_WRITE_DMA_EXT : ATA_COMMAND_WRITE_DMA);
    io_wait_400ns(channel);

    channel->awaiting_irq = true;
    channel->dma_in_progress = true;
    channel->error = false;

    outb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_COMMAND, ATA_BUSMASTER_COMMAND_START);

    ret = true;
end:
    spinlock_release(&channel->lock);
    return ret;
}

static bool ata_device_read_pio(struct ata_device* device, uint8_t* buf, size_t sector_count, uint64_t lba) {
    bool ret = false;

    struct ata_channel* channel = device->channel;
    spinlock_acquire(&channel->lock);

    bool need_lba48 = ata_channel_set_sectors(channel, sector_count, lba, device->is_secondary);

    outb(channel->io_base + ATA_REGISTER_COMMAND, need_lba48 ? ATA_COMMAND_READ_SECTORS_EXT : ATA_COMMAND_READ_SECTORS);

    uint8_t status;

    for (size_t i = 0; i < sector_count; i++) {
        do {
            status = inb(channel->io_base + ATA_REGISTER_STATUS);
        } while(status & ATA_STATUS_BUSY);

        if (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) {
            goto end;
        }

        insw(channel->io_base + ATA_REGISTER_DATA, (uint16_t*) buf, device->sector_size / sizeof(uint16_t));
        buf += device->sector_size;
    }

    ret = true;
end:
    spinlock_release(&channel->lock);
    return ret;
}

static bool ata_device_write_pio(struct ata_device* device, const uint8_t* buf, size_t sector_count, uint64_t lba) {
    bool ret = false;

    struct ata_channel* channel = device->channel;
    spinlock_acquire(&channel->lock);

    bool need_lba48 = ata_channel_set_sectors(channel, sector_count, lba, device->is_secondary);

    outb(channel->io_base + ATA_REGISTER_COMMAND, need_lba48 ? ATA_COMMAND_WRITE_SECTORS_EXT : ATA_COMMAND_WRITE_SECTORS);

    uint8_t status;

    for (size_t i = 0; i < sector_count; i++) {
        do {
            status = inb(channel->io_base + ATA_REGISTER_STATUS);
        } while(status & ATA_STATUS_BUSY);

        if (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) {
            goto end;
        }

        outsw(channel->io_base + ATA_REGISTER_DATA, (uint16_t*) buf, device->sector_size / sizeof(uint16_t));
        buf += device->sector_size;
    }

    outb(channel->io_base + ATA_REGISTER_COMMAND, ATA_COMMAND_FLUSH_CACHE);

    do {
        status = inb(channel->io_base + ATA_REGISTER_STATUS);
    } while (status & ATA_STATUS_BUSY);

    if (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) {
        goto end;
    }

    ret = true;
end:
    spinlock_release(&channel->lock);
    return ret;
}

static void ata_irq_handler(struct registers* r, void* ctx) {
    (void) r;

    struct ata_channel* channel = (struct ata_channel*) ctx;

    uint8_t busmaster_status = inb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_STATUS);
    if (busmaster_status & ATA_BUSMASTER_STATUS_INTERRUPT) {
        if (busmaster_status & ATA_BUSMASTER_STATUS_ERROR) {
            channel->error = true;
        }

        outb(channel->busmaster_base + ATA_BUSMASTER_REGISTER_STATUS, busmaster_status);

        uint8_t status = inb(channel->io_base + ATA_REGISTER_STATUS);
        if (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) {
            channel->error = true;
        }

        channel->awaiting_irq = false;
    }

    lapic_eoi();
}

static int ata_sync(struct vfs_node* node) {
    struct ata_device* device = (struct ata_device*) node->private;
    if (!ata_device_flush_cache(device)) {
        return -EIO;
    }
    return 0;
}

static ssize_t ata_read(struct vfs_node* node, void* buf, off_t offset, size_t count, int flags) {
    (void) flags;

    if (count == 0) {
        return 0;
    }

    struct ata_device* device = (struct ata_device*) node->private;
    if (device->type != ATA_DEVICE_TYPE_PATA && device->type != ATA_DEVICE_TYPE_SATA) {
        return -EPERM;
    }

    size_t sector_count = count / device->sector_size;
    uint64_t lba = offset / device->sector_size;

    if (device->dma_supported) {
        uint8_t* buf_u8 = (uint8_t*) buf;

        size_t sectors_per_dma_page = PAGE_SIZE / device->sector_size;
        size_t bulk_sectors = sector_count / sectors_per_dma_page;
        size_t leftover_sectors = sector_count % sectors_per_dma_page;

        for (size_t i = 0; i < bulk_sectors; i++) {
            if (!ata_device_read_dma(device, buf_u8, sectors_per_dma_page, lba)) {
                return -EIO;
            }
            buf_u8 += PAGE_SIZE;
            lba += sectors_per_dma_page;
        }

        if (leftover_sectors) {
            if (!ata_device_read_dma(device, buf_u8, leftover_sectors, lba)) {
                return -EIO;
            }
        }
    } else {
        if (!ata_device_read_pio(device, (uint8_t*) buf, sector_count, lba)) {
            return -EIO;
        }
    }

    return count;
}

static ssize_t ata_write(struct vfs_node* node, const void* buf, off_t offset, size_t count, int flags) {
    (void) flags;

    if (count == 0) {
        return 0;
    }

    struct ata_device* device = (struct ata_device*) node->private;
    if (device->type != ATA_DEVICE_TYPE_PATA && device->type != ATA_DEVICE_TYPE_SATA) {
        return -EPERM;
    }

    size_t sector_count = count / device->sector_size;
    uint64_t lba = offset / device->sector_size;

    if (device->dma_supported) {
        const uint8_t* buf_u8 = (const uint8_t*) buf;

        size_t sectors_per_dma_page = PAGE_SIZE / device->sector_size;
        size_t bulk_sectors = sector_count / sectors_per_dma_page;
        size_t leftover_sectors = sector_count % sectors_per_dma_page;

        for (size_t i = 0; i < bulk_sectors; i++) {
            if (!ata_device_write_dma(device, buf_u8, sectors_per_dma_page, lba)) {
                return -EIO;
            }
            buf_u8 += PAGE_SIZE;
            lba += sectors_per_dma_page;
        }

        if (leftover_sectors) {
            if (!ata_device_write_dma(device, buf_u8, leftover_sectors, lba)) {
                return -EIO;
            }
        }
    } else {
        if (!ata_device_write_pio(device, (const uint8_t*) buf, sector_count, lba)) {
            return -EIO;
        }
    }

    return count;
}

static void ata_init(struct pci_device* dev) {
    klog("[ata] ATA controller detected\n");

    if (!(dev->prog_if & 0x80)) {
        klog("[ata] ATA controller does not support DMA\n");
        return;
    }

    // TODO: support non-ISA IRQs
    uint8_t prog_if = dev->prog_if;
    if (prog_if & 0x05) {
        if (prog_if & 0x03) {
            prog_if &= ~0x01;
        }

        if (prog_if & 0x0c) {
            prog_if &= ~0x04;
        }

        if (prog_if & 0x05) {
            klog("[ata] ATA controller uses unsupported non-ISA IRQs\n");
            return;
        }

        pci_write_prog_if(dev, prog_if);
    }

    uint16_t io_base1 = 0x1f0;
    uint16_t control_base1 = 0x3f6;

    if (prog_if & 0x01) {
        struct pci_bar bar0 = pci_get_bar(dev, 0);
        io_base1 = bar0.base_address & 0xfffc;
        struct pci_bar bar1 = pci_get_bar(dev, 1);
        control_base1 = bar1.base_address & 0xfffc;
    }

    uint16_t io_base2 = 0x170;
    uint16_t control_base2 = 0x376;

    if (prog_if & 0x04) {
        struct pci_bar bar2 = pci_get_bar(dev, 2);
        io_base2 = bar2.base_address & 0xfffc;
        struct pci_bar bar3 = pci_get_bar(dev, 3);
        control_base2 = bar3.base_address & 0xfffc;
    }

    struct pci_bar bar4 = pci_get_bar(dev, 4);
    uint16_t busmaster_base = bar4.base_address & 0xfffc;

    pci_write_command_flags(dev, PCI_COMMAND_FLAG_BUSMASTER);

    uintptr_t prdt_paddr = pmm_allocz(1);

    struct ata_channel* channel0 = kmalloc(sizeof(struct ata_channel));
    if (channel0 == NULL) {
        kpanic(NULL, false, "failed to allocate memory for ata_channel struct");
    }
    channel0->io_base = io_base1;
    channel0->control_base = control_base1;
    channel0->busmaster_base = busmaster_base;
    channel0->irq = ATA_ISA_IRQ0;
    channel0->prdt_paddr = prdt_paddr;
    channel0->dma_area_paddr = pmm_allocz(1);

    struct ata_channel* channel1 = kmalloc(sizeof(struct ata_channel));
    if (channel1 == NULL) {
        kpanic(NULL, false, "failed to allocate memory for ata_channel struct");
    }
    channel1->io_base = io_base2;
    channel1->control_base = control_base2;
    channel1->busmaster_base = busmaster_base + 8;
    channel1->irq = ATA_ISA_IRQ1;
    channel1->prdt_paddr = prdt_paddr + 8;
    channel1->dma_area_paddr = pmm_allocz(1);

    software_reset_channel(channel0);
    software_reset_channel(channel1);

    isr_install_handler(IRQ(channel0->irq), ata_irq_handler, channel0);
    ioapic_redirect_irq(channel0->irq, IRQ(channel0->irq));
    ioapic_set_irq_mask(channel0->irq, false);

    isr_install_handler(IRQ(channel1->irq), ata_irq_handler, channel1);
    ioapic_redirect_irq(channel1->irq, IRQ(channel1->irq));
    ioapic_set_irq_mask(channel1->irq, false);

    ata_channel_identify_device(channel0, false);
    ata_channel_identify_device(channel0, true);
    ata_channel_identify_device(channel1, false);
    ata_channel_identify_device(channel1, true);
}

struct pci_driver ata_driver = {
    .init = ata_init,
    .name = "ata",
    .match_condition = PCI_DRIVER_MATCH_CLASS | PCI_DRIVER_MATCH_SUBCLASS,
    .match_data = {
        .class = 0x01,
        .subclass = 0x01,
    },
};
