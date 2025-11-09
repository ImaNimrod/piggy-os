#ifndef _NVME_DEFINITIONS_H
#define _NVME_DEFINITIONS_H

#define CAP_COMMANDSET_NVM (1 << 0)

#define CC_COMMANDSET_NVM           0
#define CC_ARBITRATION_ROUNDROBIN   0

#define GET_DOORBELL(bar0, id, completion, stride) (uint32_t*) ((uintptr_t) (bar0) + 0x1000 + (((id) * 2 + (completion)) * (4 << (stride))))

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
    uint32_t dword0;
    uint32_t namespace;
    uint64_t : 64;
    uint64_t metadata;
    uint64_t datapointer[2];
    uint32_t command[6];
} __attribute__((packed));

struct completion_entry {
    uint32_t value;
    uint32_t : 32;
    uint32_t subinfo;
    uint32_t cmdinfo;
} __attribute__((packed));

struct queue_descriptor {
    void* address;
    size_t entry_count;
    int index;
    uint32_t *doorbell;
    int phase;
};

struct queue_pair {
    struct nvme_controller* controller;

    struct queue_descriptor submission;
    struct queue_descriptor completion;

    spinlock_t lock;
};

struct nvme_controller {
    struct nvme_bar* bar;
    size_t doorbell_stride;
    size_t max_entries;
    struct queue_pair admin_queues;
};


#endif /* _NVME_DEFINITIONS_H */
