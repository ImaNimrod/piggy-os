#include <cpu/asm.h>
#include <cpu/isr.h>
#include <cpu/smp.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

#include "definitions.h"

struct controller_identify {
    uint16_t vid;
    uint16_t ssvid;
    uint8_t sn[20];
    uint8_t mn[40];
    uint64_t fr;
    uint8_t rab;
    uint8_t ieee[3];
    uint8_t cmic;
    uint8_t mdts;
    uint16_t cntlid;
    uint32_t ver;
    uint32_t rtd3r;
    uint32_t rtd3e;
    uint32_t oaes;
    uint32_t ctratt;
    uint16_t rrls;
    uint8_t reserved1[9];
    uint8_t cntrltype;
    uint64_t fguid[2];
    uint16_t crdt[3];
    uint8_t reserved2[106];
    uint8_t mec[16];
    uint16_t oacs;
    uint8_t acl;
    uint8_t aerl;
    uint8_t frmw;
    uint8_t lpa;
    uint8_t elpe;
    uint8_t npss;
    uint8_t avscc;
    uint8_t apsta;
    uint16_t wctemp;
    uint16_t cctemp;
    uint16_t mtfa;
    uint32_t hmpre;
    uint32_t hmmin;
    uint64_t tnvmcap[2];
    uint64_t unvmcap[2];
    uint32_t rpmbs;
    uint16_t edstt;
    uint8_t dsto;
    uint8_t fwug;
    uint16_t kas;
    uint16_t hctma;
    uint16_t mntmt;
    uint16_t mxtmt;
    uint32_t sanicap;
    uint32_t hmminds;
    uint16_t hmmaxd;
    uint16_t nsetidmax;
    uint16_t endgidmax;
    uint8_t anatt;
    uint8_t anacap;
    uint32_t anagrpmax;
    uint32_t nanagrpid;
    uint32_t pels;
    uint8_t reserved3[156];
    uint8_t sqes;
    uint8_t cqes;
    uint16_t maxcmd;
    uint32_t nn;
    uint16_t oncs;
    uint16_t fuses;
    uint8_t fna;
    uint8_t vwc;
    uint16_t awun;
    uint16_t awupf;
    uint8_t icsvscc;
    uint8_t nwpc;
    uint16_t acwu;
} __attribute__((packed));

static int controller_id = 0;

bool identify(struct nvme_controller* controller, uint32_t namespace, int subject, uintptr_t buffer_paddr) {
    struct entry_pair entry_pair = {};
    entry_pair.submission.opcode = NVME_ADMIN_OP_IDENTIFY;
    entry_pair.submission.nsid = namespace;
    entry_pair.submission.prp[0] = buffer_paddr;
    entry_pair.submission.command[0] = subject;
    return run_command(&controller->admin_queue, &entry_pair);
}

bool run_command(struct queue_pair* queue_pair, struct entry_pair* entry_pair) {
    semaphore_wait(&queue_pair->entry_semaphore);

    bool int_state = spinlock_acquire_irqsave(&queue_pair->lock);

    uint16_t pair = 0;
    while (queue_pair->entries[pair] != NULL) {
        pair++;
    }

    entry_pair->submission.cid = pair;
    entry_pair->thread = this_cpu()->scheduler.current_thread;
    queue_pair->entries[pair] = entry_pair;

    struct submission_entry* sq_entry = queue_pair->submission.address;
    sq_entry += queue_pair->submission.index++;

    *sq_entry = entry_pair->submission;

    queue_pair->submission.index %= queue_pair->submission.entry_count;

    mmio_write32(queue_pair->submission.doorbell, queue_pair->submission.index);

    scheduler_prepare_wait(this_cpu()->scheduler.current_thread, true);

    spinlock_release_irqsave(&queue_pair->lock, int_state);

    scheduler_yield();
    return entry_pair->completion.status == 0;
}

static bool setup_io_queue_pair(struct nvme_controller* controller, uint16_t id) {
    struct queue_pair* queue_pair = &controller->io_queues[id - 1];

    size_t cq_page_count = DIV_CEIL(QUEUE_ENTRY_COUNT * sizeof(struct completion_entry), PAGE_SIZE_4KB);
    uintptr_t cq_paddr = pmm_alloc_zero(cq_page_count);

    struct entry_pair entry_pair = {};
    entry_pair.submission.opcode = NVME_ADMIN_OP_COMPLETION_QUEUE;
    entry_pair.submission.prp[0] = cq_paddr;
    entry_pair.submission.command[0] = id | ((QUEUE_ENTRY_COUNT - 1) << 16);
    entry_pair.submission.command[1] = (1 << 0) | (1 << 1) | ((uint32_t) id << 16);

    if (!run_command(&controller->admin_queue, &entry_pair)) {
        return false;
    }

    queue_pair->completion = (struct queue_descriptor) {
        .address = (void*) (cq_paddr + HIGH_VMA),
        .entry_count = QUEUE_ENTRY_COUNT,
        .doorbell = CQ_DOORBELL(controller->bar, id, controller->doorbell_stride),
        .phase = 1,
    };

    size_t sq_page_count = DIV_CEIL(QUEUE_ENTRY_COUNT * sizeof(struct submission_entry), PAGE_SIZE_4KB);
    uintptr_t sq_paddr = pmm_alloc_zero(sq_page_count);

    memset(&entry_pair, 0, sizeof(struct entry_pair));
    entry_pair.submission.opcode = NVME_ADMIN_OP_SUBMISSION_QUEUE;
    entry_pair.submission.prp[0] = sq_paddr;
    entry_pair.submission.command[0] = id | ((QUEUE_ENTRY_COUNT - 1) << 16);
    entry_pair.submission.command[1] = (1 << 0) | ((uint32_t) id << 16);

    if (!run_command(&controller->admin_queue, &entry_pair)) {
        return false;
    }

    queue_pair->submission = (struct queue_descriptor) {
        .address = (void*) (sq_paddr + HIGH_VMA),
        .entry_count = QUEUE_ENTRY_COUNT,
        .doorbell = SQ_DOORBELL(controller->bar, id, controller->doorbell_stride),
    };

    semaphore_init(&queue_pair->entry_semaphore, QUEUE_ENTRY_COUNT);
    spinlock_init(&queue_pair->lock);
    return true;
}

static void nvme_irq_handler(struct registers* r, void* arg) {
    (void) r;

    struct queue_pair* queue_pair = arg;

    bool int_state = spinlock_acquire_irqsave(&queue_pair->lock);

    struct completion_entry* queue = queue_pair->completion.address;

    uint16_t count = 0;
    while (queue[queue_pair->completion.index].phase == queue_pair->completion.phase) {
        int subid = queue[queue_pair->completion.index].cid;
        queue_pair->entries[subid]->completion = queue[queue_pair->completion.index];

        scheduler_wakeup(queue_pair->entries[subid]->thread, 0);
        queue_pair->entries[subid] = NULL;
        semaphore_signal(&queue_pair->entry_semaphore);

        queue_pair->completion.index++;
        queue_pair->completion.index %= queue_pair->completion.entry_count;
        if (queue_pair->completion.index == 0) {
            queue_pair->completion.phase = !queue_pair->completion.phase;
        }

        count++;
    }

    if (count != 0) {
        mmio_write32(queue_pair->completion.doorbell, queue_pair->completion.index);
    }

    spinlock_release_irqsave(&queue_pair->lock, int_state);
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

    pci_set_command_flags(pci_dev, PCI_COMMAND_FLAG_MEMORY_SPACE | PCI_COMMAND_FLAG_BUSMASTER | PCI_COMMAND_FLAG_INTX_DISABLE, true);
    pci_set_command_flags(pci_dev, PCI_COMMAND_FLAG_IO_SPACE, false);

    struct nvme_bar* nvme_bar = (void*) (bar0.base_address + HIGH_VMA);

    uint32_t vs = mmio_read32(&nvme_bar->vs);
    uint16_t major = (vs >> 16) & 0xffff;
    uint8_t minor = (vs >> 8) & 0xff;
    uint8_t patch = vs & 0xff;

    klog("[nvme] found NVMe controller v%u.%u.%u [%04x:%04x]\n",
            major, minor, patch, pci_dev->vendor_id, pci_dev->device_id);

    // Ensure that we support this controller
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

    // Reset controller
    if (cap & (1ul << 36)) {
        mmio_write32(&nvme_bar->rst, 0x4e564d65);
    }

    mmio_write32(&nvme_bar->cc, mmio_read32(&nvme_bar->cc) & ~(1 << 0));
    while (mmio_read32(&nvme_bar->csts) & (1 << 0)) {
        pause();
    }

    // Setup minimal configuration
    uint32_t cc = mmio_read32(&nvme_bar->cc);
    cc = (cc & ~0x7f) | (CC_COMMANDSET_NVM << 4);
    cc = (cc & ~0x780) | ((LOG2(PAGE_SIZE_4KB) - 12) << 7);
    cc = (cc & ~0x3800) | (CC_ARBITRATION_ROUNDROBIN << 11);
    mmio_write32(&nvme_bar->cc, cc);

    // Setup admin queues
    uint32_t aqattr = 0;
    aqattr = (aqattr & ~0xfff) | ((PAGE_SIZE_4KB / sizeof(struct submission_entry)) - 1);
    aqattr = (aqattr & ~0xfff0000) | (((PAGE_SIZE_4KB / sizeof(struct completion_entry)) - 1) << 16);
    mmio_write32(&nvme_bar->aqattr, aqattr);

    uintptr_t admin_queues_paddr = pmm_alloc_zero(2);
    mmio_write64(&nvme_bar->asqbase, admin_queues_paddr);
    mmio_write64(&nvme_bar->acqbase, admin_queues_paddr + PAGE_SIZE_4KB);

    // Renable controller
    cc |= (1 << 0);
    mmio_write32(&nvme_bar->cc, cc);
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
    controller->id = controller_id++;
    controller->bar = nvme_bar;
    controller->doorbell_stride = 4 << ((cap >> 32) & 0x0f);
    controller->max_entries = cap & 0xff;

    controller->admin_queue.submission.address = (void*) (admin_queues_paddr + HIGH_VMA);
    controller->admin_queue.submission.entry_count = PAGE_SIZE_4KB / sizeof(struct submission_entry);
    controller->admin_queue.submission.doorbell = SQ_DOORBELL(nvme_bar, 0, controller->doorbell_stride);

    controller->admin_queue.completion.address = (void*) (admin_queues_paddr + PAGE_SIZE_4KB + HIGH_VMA);
    controller->admin_queue.completion.entry_count = PAGE_SIZE_4KB / sizeof(struct completion_entry);
    controller->admin_queue.completion.doorbell = CQ_DOORBELL(nvme_bar, 0, controller->doorbell_stride);
    controller->admin_queue.completion.phase = 1;

    semaphore_init(&controller->admin_queue.entry_semaphore, PAGE_SIZE_4KB / sizeof(struct submission_entry));

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for NVMe admin queue");
    }
    isr_register_handler(vector, nvme_irq_handler, &controller->admin_queue);

    pci_setup_msix(pci_dev, 0, vector);
    pci_set_msix_mask(pci_dev, 0, false);

    uintptr_t identity_paddr = pmm_alloc(1);
    if (!identify(controller, 0, IDENTIFY_CONTROLLER, identity_paddr)) {
        klog("[nvme] unable to identify NVMe controller\n");
        goto error;
    }

    struct controller_identify* controller_identify = (void*) (identity_paddr + HIGH_VMA);
    if (controller_identify->cntrltype != 0 && controller_identify->cntrltype != 1) {
        klog("[nvme] NVMe controller is not an I/O controller\n");
        goto error;
    }

    uint8_t min_sqlog2 = controller_identify->sqes & 0x0f;
    uint8_t max_sqlog2 = (controller_identify->sqes >> 4) & 0x0f;
    uint8_t min_cqlog2 = controller_identify->cqes & 0x0f;
    uint8_t max_cqlog2 = (controller_identify->cqes >> 4) & 0x0f;

    uint8_t sqlog2 = LOG2(sizeof(struct submission_entry));
    uint8_t cqlog2 = LOG2(sizeof(struct completion_entry));

    if (sqlog2 < min_sqlog2 || sqlog2 > max_sqlog2) {
        klog("[nvme] NVMe controller does not support the requested submission queue entry size\n");
        goto error;
    }
    if (cqlog2 < min_cqlog2 || cqlog2 > max_cqlog2) {
        klog("[nvme] NVMe controller does not support the requested completion queue entry size\n");
        goto error;
    }

    // Configure I/O submission and completion queue sizes
    cc = (cc & ~0xf0000) | (sqlog2 << 16);
    cc = (cc & ~0xf00000) | (cqlog2 << 20);
    mmio_write32(&nvme_bar->cc, cc);

    if (!identify(controller, 0, IDENTIFY_NAMESPACELIST, identity_paddr)) {
        klog("[nvme] unable to read NVMe controller namespace list\n");
        goto error;
    }

    // Allocate I/O queues
    size_t io_queue_min = MIN(pci_dev->msix_irq_count, cpu_count) - 1;

    struct entry_pair entry_pair = {};
    entry_pair.submission.opcode = NVME_ADMIN_OP_SET_FEATURES;
    entry_pair.submission.command[0] = 0x07;
    entry_pair.submission.command[1] = (io_queue_min << 16) | io_queue_min;

    if (!run_command(&controller->admin_queue, &entry_pair)) {
        klog("[nvme] unable to allocate I/O queues for NVMe controller\n");
        goto error;
    }

    size_t sq_count = entry_pair.completion.value & 0xffff;
    size_t cq_count = (entry_pair.completion.value >> 16) & 0xffff;

    controller->io_queue_count = MIN(2, MIN(sq_count + 1, cq_count + 1));
    controller->io_queues = (void*) (pmm_alloc(DIV_CEIL(sizeof(struct queue_pair) * controller->io_queue_count, PAGE_SIZE_4KB)) + HIGH_VMA);

    // Create and initialize I/O queues
    for (uint16_t i = 1; i <= controller->io_queue_count; i++) {
        if (!setup_io_queue_pair(controller, i)) {
            klog("[nvme] unable to initialize I/O queue for NVMe controller\n");
            kfree(controller->io_queues);
            goto error;
        }

        if (unlikely(!isr_allocate_vector(&vector))) {
            kpanic(NULL, false, "failed to allocate IRQ vector for NVMe I/O queue");
        }
        isr_register_handler(vector, nvme_irq_handler, &controller->io_queues[i - 1]);

        pci_setup_msix(pci_dev, i, vector);
        pci_set_msix_mask(pci_dev, i, false);
    }

    uint32_t* namespace_list = (void*) (identity_paddr + HIGH_VMA);
    for (uint32_t i = 0; i < 1024 && namespace_list[i] != 0; i++) {
        namespace_init(controller, namespace_list[i]);
    }

    pmm_free(identity_paddr, 1);

    klog("[nvme] initialized NVMe controller\n");
    return;

error:
    pmm_free(identity_paddr, 1);
    pmm_free(admin_queues_paddr, 2);
    kfree(controller);
}

struct pci_driver nvme_driver = {
    .init = nvme_init,
    .name = "nvme",
    .match_condition = PCI_DRIVER_MATCH_ADDRESS,
    .match_data = {
        .class = 0x01,
        .subclass = 0x08,
        .prog_if = 0x02,
    },
};
