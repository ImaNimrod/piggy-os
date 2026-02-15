#ifndef _XHCI_DEFINITIONS_H
#define _XHCI_DEFINITIONS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/process.h>
#include <utils/spinlock.h>
#include <utils/vector.h>

#define USB_LEGACY_BOS  (1 << 16)
#define USB_LEGACY_OOS  (1 << 24)

#define PORTSC_CCS      (1 << 0)
#define PORTSC_PED      (1 << 1)
#define PORTSC_PR       (1 << 4)
#define PORTSC_PP       (1 << 9)
#define PORTSC_CSC      (1 << 17)
#define PORTSC_WRC      (1 << 19)
#define PORTSC_PRC      (1 << 21)
#define PORTSC_WPR      (1 << 31)

#define USBCMD_RS       (1 << 0)
#define USBCMD_HCRST    (1 << 1)
#define USBCMD_INTE     (1 << 2)

#define USBSTS_HCH      (1 << 0)
#define USBSTS_EINT     (1 << 3)
#define USBSTS_CNR      (1 << 11)

#define IMAN_IP         (1 << 0)
#define IMAN_IE         (1 << 1)

#define TRB_TYPE_LINK                       6
#define TRB_TYPE_ENABLE_SLOT                9
#define TRB_TYPE_ADDRESS_DEVICE             11
#define TRB_TYPE_TRANSFER_EVENT             32
#define TRB_TYPE_COMMAND_COMPLETION_EVENT   33
#define TRB_TYPE_PORT_STATUS_CHANGE_EVENT   34

#define TRB_STATUS_SUCCESS  1

struct capability_registers {
    uint8_t caplength;
    uint8_t : 8;
    uint16_t hciversion;
    uint32_t hcsparams1;
    uint32_t hcsparams2;
    uint32_t hcsparams3;
    uint32_t hccparams1;
    uint32_t dboff;
    uint32_t rstoff;
    uint32_t hccparams2;
} __attribute__((packed));

struct port_registers {
    uint32_t portsc;
    uint32_t portpmsc;
    uint32_t portli;
    uint32_t porthlpmc;
} __attribute__((packed));

struct operational_registers {
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t reserved[2];
    uint32_t pagesize;
    uint32_t dnctrl;
    uint64_t crcr;
    uint32_t reserved2[4];
    uint64_t dcbaap;
    uint32_t config;
    uint8_t reserved3[0x400-0x3c];
    struct port_registers port_registers[];
} __attribute__((packed));

struct interrupter_registers {
    uint32_t iman;
    uint32_t imod;
    uint32_t erstsz;
    uint32_t : 32;
    uint64_t erstba;
    uint64_t erdp;
} __attribute__((packed));

struct runtime_registers {
    uint32_t mfindex;
    uint32_t reserved[7];
    struct interrupter_registers interrupter_registers[];
} __attribute__((packed));

struct trb {
    uint64_t parameter;
    uint32_t status;
    union {
        struct {
            uint32_t cycle: 1;
            uint32_t toggle_cycle: 1;
            uint32_t : 8;
            uint32_t trb_type: 6;
            uint32_t : 16;
        };
        uint32_t control;
    };
} __attribute__((packed));

struct event_ring_table_entry {
    uint64_t rsba;
    uint32_t rsz;
    uint32_t reserved;
} __attribute__((packed));

struct completion_waiter {
    uintptr_t submission_trb_paddr;
    struct trb* completion_trb;
    struct thread* thread;
};

struct xhci_ring {
    uintptr_t trb_paddr;
    struct trb* trbs;
    uint32_t size;
    uint32_t index;
    bool cycle;

    uint32_t* doorbell;
    uint32_t doorbell_value;

    vector_t* completion_waiters;

    spinlock_t lock;
};

struct port_protocol_info {
    uint8_t port_start;
    uint8_t port_end;
    uint8_t version_major;
    uint8_t version_minor;
};

struct xhci_controller {
    struct capability_registers* capability_registers;
    struct operational_registers* operational_registers;
    struct runtime_registers* runtime_registers;
    uint32_t* doorbell_registers;

    uint8_t port_count;
    uint8_t slot_count;

    vector_t* port_protocol_info;

    uint64_t* dcbaa;

    struct xhci_ring command_ring;
    struct xhci_ring event_ring;

    vector_t* devices;
};

struct xhci_device {
    uint8_t slot_id;

    uintptr_t device_context_paddr;
    uintptr_t input_context_paddr;

    struct xhci_ring ep_rings[31];
};

void device_try_init(struct xhci_controller* controller, uint8_t port);

struct trb* ring_dequeue(struct xhci_ring* ring);
void ring_init(struct xhci_ring* ring, uint32_t* doorbell, uint32_t doorbell_value);
void ring_submit(struct xhci_ring* ring, struct trb* submission_trb);
bool ring_submit_and_wait(struct xhci_ring* ring, struct trb* submission_trb, struct trb* completion_trb);

#endif /* _XHCI_DEFINITIONS_H */
