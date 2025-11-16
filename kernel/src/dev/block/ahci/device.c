#include <cpu/asm.h>
#include <cpu/smp.h>
#include <dev/block/ahci.h>
#include <dev/block/block.h>
#include <dev/hpet.h>
#include <errno.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <stdbool.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/macros.h>

#include "definitions.h"
#include "../../../utils/printf/printf.h"

static dev_t ahci_device_minor;

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

static int find_command_slot(struct ahci_device* device) {
    uint32_t slots = mmio_read32(&device->hba_port->sact) | mmio_read32(&device->hba_port->ci);

    int i = -1;
    for (i = 0; i < device->controller->slot_count; i++) {
        if (!(slots & (1 << i))) {
            break;
        }
    }

    return i;
}

static bool identify(struct ahci_device* device, uintptr_t identify_buffer_paddr) {
    spinlock_acquire(&device->lock);

    int slot = find_command_slot(device);
    if (slot == -1) {
        spinlock_release(&device->lock);
        return false;
    }

    struct hba_command_header* header = (void*) (device->clb_and_fis_paddr + HIGH_VMA);
    header += slot;

    header->cfl = sizeof(struct hba_fis_h2d)  / sizeof(uint32_t);
    header->w = false;
    header->prdtl = 1;
    header->prdbc = 0;

    struct hba_command_table* table = (void*) (device->command_table_paddr + HIGH_VMA);
    table += slot;

    struct hba_prdt* prdt = &table->prdt[0];
    prdt->dba = (uint32_t) identify_buffer_paddr;
    prdt->dbau = (uint32_t) (identify_buffer_paddr >> 32);
    prdt->dbc = 512 - 1;

    struct hba_fis_h2d* fis = (void*) &table->cfis;
    fis->type = FIS_TYPE_REG_H2D;
    fis->c = true;
    fis->command = ATA_COMMAND_IDENTIFY_DEVICE;
    fis->lba0 = fis->lba1 = fis->lba2 = fis->lba3 = fis->lba4 = fis->lba5 = 0;
    fis->countl = 0;
    fis->device = (1 << 6);

    struct hba_port* hba_port = device->hba_port;

    while ((mmio_read32(&hba_port->tfd) & (HBA_PxTFD_BSY & HBA_PxTFD_DRQ))) {
        pause();
    }

    mmio_write32(&hba_port->ci, mmio_read32(&hba_port->ci) | (1 << slot));

    spinlock_release(&device->lock);

    while (mmio_read32(&hba_port->ci) & (1 << slot)) {
        if (mmio_read32(&hba_port->is) & (1 << 30)) {
            return false;
        }
        hpet_sleep_ns(MS_TO_NS(1));
    }

    if (mmio_read32(&hba_port->is) & (1 << 30)) {
        return false;
    }

    return true;
}

static void start_command_engine(struct ahci_device* device) {
    struct hba_port* hba_port = device->hba_port;

    mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) & ~HBA_PxCMD_ST);
    while (mmio_read32(&hba_port->cmd) & HBA_PxCMD_CR) {
        pause();
    }

    mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) | HBA_PxCMD_FRE);
    mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) | HBA_PxCMD_ST);
}

static void stop_command_engine(struct ahci_device* device) {
    struct hba_port* hba_port = device->hba_port;

    mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) & ~HBA_PxCMD_ST);
    while (mmio_read32(&hba_port->cmd) & HBA_PxCMD_CR) {
        pause();
    }

    mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) & ~HBA_PxCMD_FRE);
}

static ssize_t ahci_device_cmd_handler(struct block_device* block_device, block_cmd_t cmd, uint64_t lba, size_t count, uintptr_t paddr) {
    struct ahci_device* device = block_device->private;

    if (device->is_lba48) {
        if (count > 65536) {
            return -EIO;
        }
    } else {
        if (count > 256) {
            return -EIO;
        }
    }

    spinlock_acquire(&device->lock);

    int slot = find_command_slot(device);
    if (slot == -1) {
        return -EIO;
    }

    struct hba_command_header* header = (void*) (device->clb_and_fis_paddr + HIGH_VMA);
    header += slot;

    struct hba_command_table* table = (void*) (device->command_table_paddr + HIGH_VMA);
    table += slot;

    header->cfl = sizeof(struct hba_fis_h2d) / sizeof(uint32_t);
    header->w = cmd == CMD_WRITE;

    struct hba_fis_h2d* fis = (void*) &table->cfis;
    fis->type = FIS_TYPE_REG_H2D;
    fis->c = true;

    if (cmd == CMD_FLUSH) {
        (void) lba;
        (void) count;
        (void) paddr;

        header->prdtl = header->prdbc = 0;
        fis->command = device->is_lba48 ? ATA_COMMAND_FLUSH_CACHE_EXT : ATA_COMMAND_FLUSH_CACHE;
    } else {
        size_t prdt_count = (uint16_t) ((count - 1) >> 4) + 1;
        if (prdt_count > PRDT_PER_COMMAND) {
            spinlock_release(&device->lock);
            return -EIO;
        }

        header->prdtl = prdt_count;
        header->prdbc = 0;

        size_t bytes_per_prdt = SECTORS_PER_PRDT * block_device->block_size;

        uint16_t i;
        struct hba_prdt* prdt;
        for (i = 0; i < header->prdtl - 1; i++) {
            prdt = &table->prdt[i];
            prdt->dba = (uint32_t) paddr;
            prdt->dbau = (uint32_t) (paddr >> 32);
            prdt->dbc = bytes_per_prdt - 1;
            paddr += bytes_per_prdt;
            count -= SECTORS_PER_PRDT;
        }
        prdt = &table->prdt[i];
        prdt->dba = (uint32_t) paddr;
        prdt->dbau = (uint32_t) (paddr >> 32);
        prdt->dbc = (count << LOG2(block_device->block_size)) - 1;

        if (cmd == CMD_READ) {
            fis->command = device->is_lba48 ? ATA_COMMAND_READ_DMA_EXT : ATA_COMMAND_READ_DMA;
        } else if (cmd == CMD_WRITE) {
            fis->command = device->is_lba48 ? ATA_COMMAND_WRITE_DMA_EXT : ATA_COMMAND_WRITE_DMA;
        }

        if (device->is_lba48) {
            fis->lba0 = lba & 0xff;
            fis->lba1 = (lba >> 8) & 0xff;
            fis->lba2 = (lba >> 16) & 0xff;
            fis->lba3 = (lba >> 24) & 0xff;
            fis->lba4 = (lba >> 32) & 0xff;
            fis->lba5 = (lba >> 40) & 0xff;
            fis->countl = count == 65536 ? 0 : (uint8_t) (count & 0xff);
            fis->counth = count == 65536 ? 0 : (uint8_t) ((count >> 8) & 0xff);
            fis->device = (1 << 6);
        } else {
            fis->lba0 = lba & 0xff;
            fis->lba1 = (lba >> 8) & 0xff;
            fis->lba2 = (lba >> 16) & 0xff;
            fis->countl = count == 256 ? 0 : (uint8_t) (count & 0xff);
            fis->device = (1 << 6) | ((lba >> 24) & 0x0f);
        }
    }

    struct hba_port* hba_port = device->hba_port;

    while ((mmio_read32(&hba_port->tfd) & (HBA_PxTFD_BSY & HBA_PxTFD_DRQ))) {
        pause();
    }

    device->blocked_threads[slot] = this_cpu()->running_thread;
    device->old_ci |= (1 << slot);

    mmio_write32(&hba_port->ci, mmio_read32(&hba_port->ci) | (1 << slot));

    spinlock_release(&device->lock);

    scheduler_block(this_cpu()->running_thread);

    return count;
}

void ahci_device_irq_handler(struct ahci_device* device) {
    struct hba_port* hba_port = device->hba_port;

    uint32_t is = mmio_read32(&hba_port->is);
    if (is & HBA_PxIE_ERROR_MASK) {
        klog("AHCI device error!");
    }

    uint32_t ci = mmio_read32(&hba_port->ci);

    uint32_t completed_slots = device->old_ci & ~ci;
    if (completed_slots != 0) {
        for (uint8_t i = 0; i < device->controller->slot_count; i++) {
            if (completed_slots & (1 << i)) {
                scheduler_unblock(device->blocked_threads[i]);
                device->blocked_threads[i] = NULL;
            }
        }

        spinlock_acquire(&device->lock);
        device->old_ci &= ~completed_slots;
        spinlock_release(&device->lock);
    }

    mmio_write32(&device->hba_port->is, is);
}

void ahci_device_try_init(struct ahci_controller* controller, uint8_t port_number, struct hba_port* hba_port) {
    uintptr_t clb_and_fis_paddr = pmm_alloc_zero(1);

    uintptr_t clb_paddr = clb_and_fis_paddr;
    mmio_write32(&hba_port->clb, (uint32_t) clb_paddr);
    mmio_write32(&hba_port->clbu, (uint32_t) (clb_paddr >> 32));

    uintptr_t fis_paddr = clb_and_fis_paddr + (sizeof(struct hba_command_header) * controller->slot_count);
    mmio_write32(&hba_port->fb, (uint32_t) fis_paddr);
    mmio_write32(&hba_port->fbu, (uint32_t) (fis_paddr >> 32));

    mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) | HBA_PxCMD_FRE);

    mmio_write32(&hba_port->ie, 0);
    mmio_write32(&hba_port->is, mmio_read32(&hba_port->is));

    /* if staggered spin up is supported, spin up port */
    if (mmio_read32(&controller->hba_registers->cap) & CAP_SSS) {
        mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) | HBA_PxCMD_SUD);
    }

    hpet_sleep_ns(MS_TO_NS(10));

    uint32_t ssts = mmio_read32(&hba_port->ssts);
    uint8_t det = ssts & 0xf;
    uint8_t ipm = (ssts >> 8) & 0xf;
    if (det != 3 || ipm != 1) {
        goto early_error;
    }

    mmio_write32(&hba_port->serr, mmio_read32(&hba_port->serr));

    int timeout = 100;
    while (timeout != 0) {
        if (!(mmio_read32(&hba_port->tfd) & (HBA_PxTFD_BSY | HBA_PxTFD_DRQ))) {
            break;
        }
        hpet_sleep_ns(MS_TO_NS(1));
        timeout--;
    }

    if (timeout == 0) {
        klog("[ahci] port #%u timed out during initialization\n", port_number);
        goto early_error;
    }

    ata_device_type_t type;

    uint32_t sig = mmio_read32(&hba_port->sig);
    if (sig == HBA_PxSIG_ATA) {
        type = ATA_DEVICE_TYPE_SATA;
    } else if (sig == HBA_PxSIG_ATAPI) {
        type = ATA_DEVICE_TYPE_SATAPI;
    } else {
        klog("[ahci] unknown device found on port #%u\n", port_number);
        goto early_error;
    }

    struct ahci_device* device = kmalloc(sizeof(struct ahci_device));
    if (unlikely(device == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for AHCI device");
    }
    device->controller = controller;
    device->port_number = port_number;
    device->hba_port = hba_port;

    device->type = type;

    device->clb_and_fis_paddr = clb_and_fis_paddr;
    device->command_table_paddr = pmm_alloc_zero(DIV_CEIL(sizeof(struct hba_command_table) * controller->slot_count, PAGE_SIZE_4KB));

    device->blocked_threads = kmalloc(sizeof(struct thread*) * controller->slot_count);
    if (unlikely(device->blocked_threads == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for AHCI device blocked threads");
    }

    struct hba_command_header* command_header = (void*) (clb_paddr + HIGH_VMA);
    for (uint8_t i = 0; i < controller->slot_count; i++) {
        uintptr_t paddr = device->command_table_paddr + (i * sizeof(struct hba_command_table));
        command_header[i].ctba = (uint32_t) paddr;
        command_header[i].ctbau = (uint32_t) (paddr >> 32);
        command_header[i].prdtl = PRDT_PER_COMMAND;
    }

    start_command_engine(device);

    uintptr_t identity_buffer_paddr = pmm_alloc_zero(1);
    if (identity_buffer_paddr == 0) {
        kpanic(NULL, false, "failed to allocate memory for ACHI device identity buffer");
    }

    if (!identify(device, identity_buffer_paddr)) {
        goto error;
    }

    uint16_t* identity_buffer = (void*) (identity_buffer_paddr + HIGH_VMA);

    device->is_lba48 = identity_buffer[83] & (1 << 10);

    size_t sector_count;
    if (device->is_lba48) {
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

    copy_ata_string(device->serial_number, sizeof(device->serial_number), (uint8_t*) &identity_buffer[10]);
    copy_ata_string(device->firmware_revision, sizeof(device->firmware_revision), (uint8_t*) &identity_buffer[23]);
    copy_ata_string(device->model_number, sizeof(device->model_number), (uint8_t*) &identity_buffer[27]);

    pmm_free(identity_buffer_paddr, 1);

    size_t total_size;
    if (__builtin_mul_overflow(sector_count, sector_size, &total_size)) {
        kpanic(NULL, false, "AHCI device disk size overflow");
    }

    /* renable interrupts for the port */
    mmio_write32(&hba_port->is, mmio_read32(&hba_port->is));
    mmio_write32(&hba_port->ie, HBA_PxIE_DHRE | HBA_PxIE_PSE | HBA_PxIE_DSE | HBA_PxIE_SDBE | HBA_PxIE_DPE | HBA_PxIE_ERROR_MASK);

    vector_push(controller->devices, &device);

    klog("[ahci] found %s device on port #%u (size: %zuGB, block size: %zuB)\n",
            ata_device_type_str(type), port_number, total_size / 1000000000, sector_size);

    char name[10];
    snprintf(name, sizeof(name) - 1, "ahci%zu", ahci_device_minor);

    struct block_device block_device = {
        .cmd_handler = ahci_device_cmd_handler,
        .private = device,
        .block_count = sector_count,
        .block_size = sector_size,
        .lba_offset = 0,
    };

    int ret = block_register(name, makedev(AHCI_DEV_MAJOR, ahci_device_minor), &block_device, true);
    if (ret < 0) {
        goto error;
    }

    ahci_device_minor++;
    return;

early_error:
    mmio_write32(&hba_port->cmd, mmio_read32(&hba_port->cmd) & ~HBA_PxCMD_FRE);
    pmm_free(clb_and_fis_paddr, 1);
    return;

error:
    pmm_free(identity_buffer_paddr, 1);

    stop_command_engine(device);
    pmm_free(device->command_table_paddr, DIV_CEIL(sizeof(struct hba_command_table) * controller->slot_count, PAGE_SIZE_4KB));
    pmm_free(device->clb_and_fis_paddr, 1);

    kfree(device);
}
