#ifndef _XHCI_DEFINITIONS_H
#define _XHCI_DEFINITIONS_H

#include <stdint.h>

#define PORTSC_CCS      (1 << 0)
#define PORTSC_PR       (1 << 4)
#define PORTSC_CSC      (1 << 17)
#define PORTSC_PRC      (1 << 21)

#define USBCMD_RS       (1 << 0)
#define USBCMD_HCRST    (1 << 1)
#define USBCMD_INTE     (1 << 2)

#define USBSTS_HCH      (1 << 0)
#define USBSTS_EINT     (1 << 3)
#define USBSTS_CNR      (1 << 11)

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
            uint32_t : 9;
            uint32_t trb_type: 6;
            uint32_t : 16;
        };
        uint32_t control;
    };
} __attribute__((packed));

struct event_ring_table_entry {
    uint64_t rsba;
    uint32_t rsz;
    uint32_t : 32;
} __attribute__((packed));

#endif /* _XHCI_DEFINITIONS_H */
