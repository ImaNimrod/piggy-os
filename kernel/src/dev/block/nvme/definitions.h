#ifndef _NVME_DEFINITIONS_H
#define _NVME_DEFINITIONS_H

#include <dev/block/nvme.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/process.h>
#include <utils/semaphore.h>
#include <utils/spinlock.h>

#define CAP_COMMANDSET_NVM (1 << 0)

#define CC_COMMANDSET_NVM           0
#define CC_ARBITRATION_ROUNDROBIN   0

#define IDENTIFY_NAMESPACE      0
#define IDENTIFY_CONTROLLER     1
#define IDENTIFY_NAMESPACELIST  2

#define NVME_OP_FLUSH                   0x00
#define NVME_OP_WRITE                   0x01
#define NVME_OP_READ                    0x02

#define NVME_ADMIN_OP_SUBMISSION_QUEUE  0x01
#define NVME_ADMIN_OP_COMPLETION_QUEUE  0x05
#define NVME_ADMIN_OP_IDENTIFY          0x06
#define NVME_ADMIN_OP_SET_FEATURES      0x09

#define QUEUE_ENTRY_COUNT (PAGE_SIZE_4KB / sizeof(struct submission_entry))

#define SQ_DOORBELL(bar, sqid, stride) ((uint32_t*) ((uintptr_t) (bar) + 0x1000 + (sqid) * 2 * (stride)))
#define CQ_DOORBELL(bar, cqid, stride) ((uint32_t*) ((uintptr_t) (bar) + 0x1000 + (cqid) * 2 * (stride) + (stride)))

struct nvme_bar {
    uint64_t cap;
    uint32_t vs;
    uint32_t intms;
    uint32_t intmc;
    uint32_t cc;
    uint32_t : 32;
    uint32_t csts;
    uint32_t rst;
    uint32_t aqattr;
    uint64_t asqbase;
    uint64_t acqbase;
} __attribute__((packed));

struct submission_entry {
    uint8_t opcode;
    uint8_t flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t : 64;
    uint64_t metadata;
    uint64_t prp[2];
    uint32_t command[6];
} __attribute__((packed));

struct completion_entry {
    uint32_t value;
    uint32_t : 32;
    uint16_t sqhead;
    uint16_t sqid;
    uint16_t cid;
    uint16_t phase: 1;
    uint16_t status: 15;
} __attribute__((packed));

struct entry_pair {
    struct submission_entry submission;
    struct completion_entry completion;
    struct thread* thread;
};

struct queue_descriptor {
    void* address;
    size_t entry_count;
    uint16_t index;
    uint32_t* doorbell;
    uint32_t phase;
};

struct queue_pair {
    struct queue_descriptor submission;
    struct queue_descriptor completion;

    struct entry_pair* entries[QUEUE_ENTRY_COUNT];
    semaphore_t entry_semaphore;

    spinlock_t lock;
};

struct nvme_controller {
    int id;

    struct nvme_bar* bar;
    size_t doorbell_stride;
    size_t max_entries;

    struct queue_pair admin_queue;

    uint16_t io_queue_count;
    uint16_t io_queue_index;
    struct queue_pair* io_queues;
};

struct nvme_namespace {
    int id;
    struct nvme_controller* controller;
};

bool identify(struct nvme_controller* controller, uint32_t namespace, int subject, uintptr_t buffer_paddr);
bool run_command(struct queue_pair* queue_pair, struct entry_pair* entry_pair);

void namespace_init(struct nvme_controller* controller, int id);

#endif /* _NVME_DEFINITIONS_H */
