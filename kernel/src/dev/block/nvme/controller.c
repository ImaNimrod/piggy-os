#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/block/nvme.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>

#include "definitions.h"

static void nvme_irq_handler(struct registers* r, void* arg) {
    (void) r;

    struct queue_pair* pair = arg;
}

static void nvme_init(struct pci_device* pci_dev) {
    struct pci_bar bar0;
    if (!pci_get_bar(pci_dev, 0, &bar0)) {
        klog("[nvme] unable to get PCI BAR0 for NVMe controller access\n");
        return;
    }
    if (!pci_map_bar(&bar0)) {
        klog("[nvme] failed to map memory for NVMe controller access\n");
        return;
    }

    pci_write_command_flags(pci_dev, PCI_COMMAND_FLAG_MEMORY_SPACE | PCI_COMMAND_FLAG_BUSMASTER | PCI_COMMAND_FLAG_INTX_DISABLE);

    struct nvme_bar* nvme_bar = (struct nvme_bar*) (bar0.base_address + HIGH_VMA);

    uint32_t vs = mmio_read32(&nvme_bar->vs);
    uint16_t major = (vs >> 16) & 0xffff;
    uint8_t minor = (vs >> 8) & 0xff;
    uint8_t patch = vs & 0xff;

    klog("[nvme] found NVMe controller v%u.%u.%u [%04x:%04x]\n",
            major, minor, patch, pci_dev->vendor_id, pci_dev->device_id);

    /* ensure that we support this controller */
    if (major == 1 && minor < 3) {
        klog("[nvme] NVMe controller is an unsupported version\n");
        return;
    }

    uint64_t cap = mmio_read64(&nvme_bar->cap);

    uint8_t command_set = (uint8_t) ((cap >> 37) & 0xff);
    if (!(command_set & CAP_COMMANDSET_NVM)) {
        klog("[nvme] NVMe controller does not support the NVM command set\n");
        return;
    }

    uint8_t min_page_size = (uint8_t) ((cap >> 48) & 0x0f);
    uint8_t max_page_size = (uint8_t) ((cap >> 52) & 0x0f);

    if ((1 << (min_page_size + 12)) > PAGE_SIZE_4KB || (1 << (max_page_size + 12)) < PAGE_SIZE_4KB) {
        klog("[nvme] NVMe controller does not support the native CPU page size\n");
        return;
    }

    if (!pci_enable_msix(pci_dev)) {
        klog("[nvme] failed to enable MSI-X interrupts for NVMe controller\n");
        return;
    }

    /* reset controller */
    mmio_write32(&nvme_bar->cc, mmio_read32(&nvme_bar->cc) & ~(1 << 0));
    while (mmio_read32(&nvme_bar->csts) & (1 << 0)) {
        pause();
    }

    /* setup minimal configuration */
    uint32_t cc = mmio_read32(&nvme_bar->cc);
    cc = (cc & ~0x7f) | (CC_COMMANDSET_NVM << 4);
    cc = (cc & ~0x780) | ((LOG2(PAGE_SIZE_4KB) - 12) << 7);
    cc = (cc & ~0x3800) | (CC_ARBITRATION_ROUNDROBIN << 11);
    mmio_write32(&nvme_bar->cc, cc);

    /* setup admin queues */
    uint32_t aqattr = 0;
    aqattr = (aqattr & ~0xfff) | ((PAGE_SIZE_4KB / sizeof(struct submission_entry)) - 1);
    aqattr = (aqattr & ~0xfff0000) | (((PAGE_SIZE_4KB / sizeof(struct completion_entry)) - 1) << 16);
    mmio_write32(&nvme_bar->aqattr, aqattr);

    uintptr_t admin_queues_paddr = pmm_alloc_zero(2);

    mmio_write64(&nvme_bar->asqbase, admin_queues_paddr);
    mmio_write64(&nvme_bar->acqbase, admin_queues_paddr + PAGE_SIZE_4KB);

    /* renable controller */
    mmio_write32(&nvme_bar->cc, mmio_read32(&nvme_bar->cc) | (1 << 0));
    while (!(mmio_read32(&nvme_bar->csts) & 3)) {
        pause();
    }

    if (mmio_read32(&nvme_bar->csts) & (1 << 1)) {
        klog("[nvme] failed to initialize NVMe controller\n");
        pmm_free(admin_queues_paddr, 2);
        return;
    }

    struct nvme_controller* controller = kmalloc(sizeof(struct nvme_controller));
    if (unlikely(controller == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for NVMe controller");
    }
    controller->doorbell_stride = (cap >> 32) & 0x0f;
    controller->max_entries = cap & 0xff;

    controller->admin_queues.submission.address = (void*) (admin_queues_paddr + HIGH_VMA);
    controller->admin_queues.submission.entry_count = PAGE_SIZE_4KB / sizeof(struct submission_entry);
    controller->admin_queues.submission.doorbell = GET_DOORBELL(nvme_bar, 0, 0, controller->doorbell_stride);

    controller->admin_queues.completion.address = (void*) (admin_queues_paddr + PAGE_SIZE_4KB + HIGH_VMA);
    controller->admin_queues.completion.entry_count = PAGE_SIZE_4KB / sizeof(struct completion_entry);
    controller->admin_queues.completion.doorbell = GET_DOORBELL(nvme_bar, 0, 1, controller->doorbell_stride);
    controller->admin_queues.completion.phase = 1;

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for NVMe admin queues");
    }

    pci_setup_msix(pci_dev, 0, vector);
    pci_set_msix_mask(pci_dev, 0, false);

    isr_register_handler(vector, nvme_irq_handler, &controller->admin_queues);

    klog("[nvme] initialized NVMe controller\n");
}

struct pci_driver nvme_driver = {
    .init = nvme_init,
    .name = "nvme",
    .match_condition = PCI_DRIVER_MATCH_CLASS | PCI_DRIVER_MATCH_SUBCLASS | PCI_DRIVER_MATCH_PROG_IF,
    .match_data = {
        .class = 0x01,
        .subclass = 0x08,
        .prog_if = 0x02,
    },
};
