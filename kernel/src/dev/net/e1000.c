#include <cpu/asm.h>
#include <cpu/isr.h>
#include <dev/hpet.h>
#include <dev/ioapic.h>
#include <dev/pci.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <net/netif.h>
#include <net/packet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/semaphore.h>
#include <utils/spinlock.h>
#include <utils/string.h>

#define NUM_RX_DESCRIPTORS 128
#define NUM_TX_DESCRIPTORS 64

#define BUFFER_SIZE 2048

#define E1000_REG_CTRL          0x0000
#define E1000_REG_STATUS        0x0008
#define E1000_REG_EEC           0x0010
#define E1000_REG_EERD          0x0014
#define E1000_REG_CTRL_EXT      0x0018
#define E1000_REG_FLA           0x001c
#define E1000_REG_FCAL          0x0028
#define E1000_REG_FCAH          0x002c
#define E1000_REG_FCT           0x0030
#define E1000_REG_VET           0x0038
#define E1000_REG_FCTTV         0x0170
#define E1000_REG_FCRTV         0x0540
#define E1000_REG_ICR           0x00c0
#define E1000_REG_ITR           0x00c4
#define E1000_REG_ICS           0x00c8
#define E1000_REG_IMS           0x00d0
#define E1000_REG_IMC           0x00d8
#define E1000_REG_RCTL          0x0100
#define E1000_REG_TCTL          0x0400
#define E1000_REG_TIPG          0x0410
#define E1000_REG_RXDESCLO      0x2800
#define E1000_REG_RXDESCHI      0x2804
#define E1000_REG_RXDESCLEN     0x2808
#define E1000_REG_RXDESCHEAD    0x2810
#define E1000_REG_RXDESCTAIL    0x2818
#define E1000_REG_RDTR          0x2820
#define E1000_REG_RADV          0x282c
#define E1000_REG_RSRPD         0x2c00
#define E1000_REG_TXDESCLO      0x3800
#define E1000_REG_TXDESCHI      0x3804
#define E1000_REG_TXDESCLEN     0x3808
#define E1000_REG_TXDESCHEAD    0x3810
#define E1000_REG_TXDESCTAIL    0x3818
#define E1000_REG_TIDV          0x3820
#define E1000_REG_TADV          0x382c
#define E1000_REG_CRCERRS       0x4000
#define E1000_REG_MTA           0x5200

#define CTRL_FD         (1 << 0)
#define CTRL_GIO_MD     (1 << 2)
#define CTRL_LRST       (1 << 3)
#define CTRL_ASDE       (1 << 5)
#define CTRL_SLU        (1 << 6)
#define CTRL_ILOS       (1 << 7)
#define CTRL_FRCSPD     (1 << 11)
#define CTRL_FRCDPLX    (1 << 12)
#define CTRL_RST        (1 << 26)
#define CTRL_VME        (1 << 30)

#define STATUS_LU       (1 << 1)
#define STATUS_GIO_ME   (1 << 19)

#define INT_TXDW    (1 << 0)
#define INT_TXQE    (1 << 1)
#define INT_LSC     (1 << 2)
#define INT_RXDMT0  (1 << 4)
#define INT_RXO     (1 << 6)
#define INT_RXT0    (1 << 7)

#define RCTL_EN     (1 << 1)
#define RCTL_BAM    (1 << 15)
#define RCTL_SECRC  (1 << 26)

#define TCTL_EN         (1 << 1)
#define TCTL_PSP        (1 << 3)
#define TCTL_RTLC       (1 << 24)
#define TCTL_CT_SHIFT   4
#define TCTL_COLD_SHIFT 12

struct rx_descriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t error;
    uint16_t special;
} __attribute__((packed));

struct tx_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

struct e1000_device {
    struct netif* netif;

    uintptr_t mmio_base;
    bool has_eeprom;

    struct rx_descriptor* rx_descs;
    uintptr_t rx_buffer_paddr;
    uint16_t rx_tail;

    struct tx_descriptor* tx_descs;
    uintptr_t tx_buffer_paddr;
    uint16_t tx_tail;
    spinlock_t tx_lock;
    semaphore_t tx_semaphore;
};

static inline uint32_t e1000_read(struct e1000_device* device, uint16_t reg) {
    return mmio_read32((void*) (device->mmio_base + reg));
}

static inline void e1000_write(struct e1000_device* device, uint16_t reg, uint32_t value) {
    mmio_write32((void*) (device->mmio_base + reg), value);
}

static inline void e1000_flush(struct e1000_device* device) {
    e1000_read(device, E1000_REG_STATUS);
}

static bool detect_eeprom(struct e1000_device* device) {
    e1000_write(device, E1000_REG_EERD, (1 << 0));

    for (int i = 0; i < 1000; i++) {
        if (e1000_read(device, E1000_REG_EERD) & (1 << 4)) {
            return true;
        }
    }

    return false;
}

static void disable_pcie_master(struct e1000_device* device) {
    uint32_t ctrl  = e1000_read(device, E1000_REG_CTRL);
    ctrl |= CTRL_GIO_MD;
    e1000_write(device, E1000_REG_CTRL, ctrl);

    int timeout = 800;
    while (timeout != 0) {
        if (!(e1000_read(device, E1000_REG_STATUS) & STATUS_GIO_ME)) {
            break;
        }
        hpet_sleep_ns(US_TO_NS(100));
        timeout--;
    }

    if (unlikely(timeout == 0)) {
        klog("[e1000] failed to stop all pending GIO master requests\n");
    }
}

static void init_rx(struct e1000_device* device) {
    uintptr_t rx_desc_paddr = pmm_alloc_zero(DIV_CEIL(NUM_RX_DESCRIPTORS * sizeof(struct rx_descriptor), PAGE_SIZE_4KB));

    device->rx_descs = (void*) (rx_desc_paddr + HIGH_VMA);
    device->rx_tail = 0;

    e1000_write(device, E1000_REG_RXDESCLO, (uint32_t) rx_desc_paddr);
    e1000_write(device, E1000_REG_RXDESCHI, (uint32_t) (rx_desc_paddr >> 32));
    e1000_write(device, E1000_REG_RXDESCLEN, NUM_RX_DESCRIPTORS * sizeof(struct rx_descriptor));
    e1000_write(device, E1000_REG_RXDESCHEAD, 0);
    e1000_write(device, E1000_REG_RXDESCTAIL, NUM_RX_DESCRIPTORS - 1);

    uintptr_t rx_buffers = pmm_alloc_zero(DIV_CEIL(NUM_RX_DESCRIPTORS * BUFFER_SIZE, PAGE_SIZE_4KB));
    for (size_t i = 0; i < NUM_RX_DESCRIPTORS; i++) {
        struct rx_descriptor* desc = &device->rx_descs[i];
        desc->address = rx_buffers + (i * BUFFER_SIZE);
        desc->status = 0;
    }

    e1000_write(device, E1000_REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    e1000_write(device, E1000_REG_RSRPD, 0);
    e1000_write(device, E1000_REG_RADV, 0);

    e1000_flush(device);
}

static void init_tx(struct e1000_device* device) {
    uintptr_t tx_desc_paddr = pmm_alloc_zero(DIV_CEIL(NUM_TX_DESCRIPTORS * sizeof(struct tx_descriptor), PAGE_SIZE_4KB));

    device->tx_descs = (void*) (tx_desc_paddr + HIGH_VMA);
    device->tx_tail = 0;

    e1000_write(device, E1000_REG_TXDESCLO, (uint32_t) tx_desc_paddr);
    e1000_write(device, E1000_REG_TXDESCHI, (uint32_t) (tx_desc_paddr >> 32));
    e1000_write(device, E1000_REG_TXDESCLEN, NUM_TX_DESCRIPTORS * sizeof(struct tx_descriptor));
    e1000_write(device, E1000_REG_TXDESCHEAD, 0);
    e1000_write(device, E1000_REG_TXDESCTAIL, 0);

    uintptr_t tx_buffers = pmm_alloc_zero(DIV_CEIL(NUM_TX_DESCRIPTORS * BUFFER_SIZE, PAGE_SIZE_4KB));
    for (size_t i = 0; i < NUM_TX_DESCRIPTORS; i++) {
        struct tx_descriptor* desc = &device->tx_descs[i];
        desc->address = tx_buffers + (i * BUFFER_SIZE);
        desc->status = 0;
        desc->cmd = (1 << 0);
    }

    e1000_write(device, E1000_REG_TCTL, TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT) | TCTL_RTLC);
    e1000_write(device, E1000_REG_TIPG, 0x0060200a);

    e1000_flush(device);

    semaphore_init(&device->tx_semaphore, NUM_TX_DESCRIPTORS);
}

static uint16_t read_eeprom(struct e1000_device* device, uint8_t address) {
    uint32_t ret = 0;

    if (device->has_eeprom) {
        e1000_write(device, E1000_REG_EERD, (1 << 0) | ((uint32_t) (address) << 8));
        while (!((ret = e1000_read(device, E1000_REG_EERD)) & (1 << 4))) {
            pause();
        }
    }

    return (uint16_t) ((ret >> 16) & 0xffff);
}

static void read_mac_address(struct e1000_device* device) {
    struct netif* netif = device->netif;

    if (device->has_eeprom) {
        uint16_t mac01 = read_eeprom(device, 0);
        netif->mac[0] = mac01 & 0xff;
        netif->mac[1] = (mac01 >> 8) & 0xff;
        uint16_t mac23 = read_eeprom(device, 1);
        netif->mac[2] = mac23 & 0xff;
        netif->mac[3] = (mac23 >> 8) & 0xff;
        uint16_t mac45 = read_eeprom(device, 2);
        netif->mac[4] = mac45 & 0xff;
        netif->mac[5] = (mac45 >> 8) & 0xff;
    } else {
        uint32_t* mem_base_mac = (uint32_t*) (device->mmio_base + 0x5400);
        uint32_t mac0123 = mmio_read32(&mem_base_mac[0]);
        uint32_t mac45 = mmio_read32(&mem_base_mac[1]);
        netif->mac[0] = mac0123 & 0xff;
        netif->mac[1] = (mac0123 >> 8) & 0xff;
        netif->mac[2] = (mac0123 >> 16) & 0xff;
        netif->mac[3] = (mac0123 >> 24) & 0xff;
        netif->mac[4] = mac45 & 0xff;
        netif->mac[5] = (mac45 >> 8) & 0xff;
    }
}

static void reset(struct e1000_device* device) {
    e1000_write(device, E1000_REG_IMC, 0xffffffff);
    e1000_read(device, E1000_REG_ICR);

    e1000_write(device, E1000_REG_RCTL, 0);
    e1000_write(device, E1000_REG_TCTL, TCTL_PSP);

    e1000_flush(device);

    hpet_sleep_ns(MS_TO_NS(10));

    /* perform reset */
    e1000_write(device, E1000_REG_CTRL, e1000_read(device, E1000_REG_CTRL) | CTRL_RST);
    hpet_sleep_ns(US_TO_NS(1));
    while (e1000_read(device, E1000_REG_CTRL) & CTRL_RST) {
        pause();
    }

    e1000_write(device, E1000_REG_IMC, 0xffffffff);
    e1000_read(device, E1000_REG_ICR);

    e1000_flush(device);
}

static void update_link_status(struct e1000_device* device) {
    uint32_t status = e1000_read(device, E1000_REG_STATUS);
    if (status & STATUS_LU) {
        device->netif->flags |= NETIF_FLAG_RUNNING;
    } else {
        device->netif->flags &= ~NETIF_FLAG_RUNNING;
    }
}

static void e1000_irq_handler(struct registers* r, void* ctx) {
    (void) r;

    struct e1000_device* device = ctx;

    uint32_t icr = e1000_read(device, E1000_REG_ICR);
    
    /* link status change */
    if (icr & INT_LSC) {
        e1000_write(device, E1000_REG_CTRL, e1000_read(device, E1000_REG_CTRL) | CTRL_SLU | CTRL_ASDE);
        update_link_status(device);
    }

    /* packet transmitted */
    if (icr & INT_TXDW) {
        semaphore_signal(&device->tx_semaphore);
        device->netif->tx_count++;
    }

    /* packet received */
    if (icr & INT_RXT0) {
        for (;;) {
            device->rx_tail = e1000_read(device, E1000_REG_RXDESCTAIL);
            if (device->rx_tail == e1000_read(device, E1000_REG_RXDESCHEAD)) {
                break;
            }

            device->rx_tail = (device->rx_tail + 1) % NUM_RX_DESCRIPTORS;

            if (!(device->rx_descs[device->rx_tail].status & (1 << 0))) {
                break;
            }

            device->netif->rx_count++;

            const void* buf = (const void*) (device->rx_descs[device->rx_tail].address + HIGH_VMA);
            size_t length = device->rx_descs[device->rx_tail].length;
            netif_add_packet(device->netif, buf, length);

            device->rx_descs[device->rx_tail].status = 0;

            e1000_write(device, E1000_REG_RXDESCTAIL, device->rx_tail);
        }
    }

    e1000_write(device, E1000_REG_ICR, icr);
}

static bool e1000_send_packet(struct netif* netif, struct packet* packet) {
    struct e1000_device* device = netif->device;

    semaphore_wait(&device->tx_semaphore);

    spinlock_acquire(&device->tx_lock);

    struct tx_descriptor* tx_desc = &device->tx_descs[device->tx_tail];

    memcpy((void*) (tx_desc->address + HIGH_VMA), packet->buf, packet->length);
    tx_desc->length = packet->length;
    tx_desc->cmd = (1 << 0) | (1 << 1) | (1 << 3);
    tx_desc->status = 0;

    device->tx_tail = (device->tx_tail + 1) % NUM_TX_DESCRIPTORS;
    e1000_write(device, E1000_REG_TXDESCTAIL, device->tx_tail);

    spinlock_release(&device->tx_lock);
    return true;
}

static void e1000_update_flags(struct netif* netif, uint16_t old_flags) {
    if (!(netif->flags & NETIF_FLAG_UP) && netif->flags & NETIF_FLAG_DYNAMIC) {
        netif->ipv4_address = 0;
        netif->ipv4_gateway = 0;
        netif->ipv4_subnet_mask = 0;
    }

    if (old_flags & NETIF_FLAG_RUNNING && !(netif->flags & NETIF_FLAG_RUNNING)) {
        netif->flags |= NETIF_FLAG_RUNNING;
    } else if (!(old_flags & NETIF_FLAG_RUNNING) && (netif->flags & NETIF_FLAG_RUNNING)) {
        netif->flags &= ~NETIF_FLAG_RUNNING;
    }
}

static void e1000_init(struct pci_device* pci_dev) {
    klog("[e1000] found E1000 compatible network card [%04x:%04x]\n", pci_dev->vendor_id, pci_dev->device_id);

    struct pci_bar bar0;
    if (!pci_get_bar(pci_dev, 0, &bar0)) {
        klog("[e1000] unable to get PCI BAR0 for E1000 device\n");
        return;
    }

    if (unlikely(!bar0.is_mmio)) {
        klog("[e1000] E1000 device does not support MMIO access\n");
        return;
    }

    if (!pci_map_bar(&bar0)) {
        klog("[e1000] failed to map memory for E1000 device MMIO area\n");
        return;
    }

    pci_set_command_flags(pci_dev, PCI_COMMAND_FLAG_MEMORY_SPACE | PCI_COMMAND_FLAG_BUSMASTER | PCI_COMMAND_FLAG_INTX_DISABLE, true);
    pci_set_command_flags(pci_dev, PCI_COMMAND_FLAG_IO_SPACE, false);

    struct e1000_device* device = kmalloc(sizeof(struct e1000_device));
    if (unlikely(device == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for E1000 device");
    }

    device->mmio_base = bar0.base_address + HIGH_VMA;

    struct netif* netif = netif_create();
    netif->mtu = 1500;
    netif->device = device;
    netif->send_packet = e1000_send_packet;
    netif->update_flags = e1000_update_flags;

    device->netif = netif;

    if (pci_dev->is_pcie) {
        disable_pcie_master(device);
    }

    reset(device);

    /* setup device for normal operation */
    uint32_t ctrl = e1000_read(device, E1000_REG_CTRL);
    ctrl &= ~CTRL_LRST;
    ctrl &= ~CTRL_ILOS;
    ctrl &= ~CTRL_FRCSPD;
    ctrl &= ~CTRL_FRCDPLX;
    ctrl &= ~CTRL_VME;
    e1000_write(device, E1000_REG_CTRL, ctrl);

    device->has_eeprom = detect_eeprom(device);

    read_mac_address(device);
    netif->ipv4_address = IPV4_ADDRESS(192, 168, 100, 2);

    uint8_t vector;
    if (unlikely(!isr_allocate_vector(&vector))) {
        kpanic(NULL, false, "failed to allocate IRQ vector for E1000 device");
    }

    if (!pci_setup_msi(pci_dev, vector)) {
        ioapic_redirect_irq(pci_read_irq_line(pci_dev), vector);
    }

    isr_register_handler(vector, e1000_irq_handler, device);

    /* start link and enable automatic link speed detection */
    e1000_write(device, E1000_REG_CTRL, e1000_read(device, E1000_REG_CTRL) | CTRL_SLU | CTRL_ASDE);

    /* clear statistic registers */ 
    for (size_t i = 0; i < 64; i++) {
        e1000_read(device, E1000_REG_CRCERRS + (i * 4));
    }

    /* clear the multicast table array */ 
    for (size_t i = 0; i < 128; i++) {
        e1000_write(device, E1000_REG_MTA + (i * 4), 0);
    }

    init_rx(device);
    init_tx(device);

    e1000_write(device, E1000_REG_ITR, 0);

    /* renable desired interrupts */
    e1000_write(device, E1000_REG_IMS, INT_TXDW | INT_TXQE | INT_LSC | INT_RXDMT0 | INT_RXO | INT_RXT0);
    e1000_read(device, E1000_REG_ICR);

    e1000_flush(device);

    update_link_status(device);

    klog("[e1000] initialized E1000 network card (mac: " MAC_ADDRESS_FORMAT ")\n", MAC_ADDRESS_PRINT(netif->mac));

    if (pci_dev->msi_supported) {
        pci_set_msi_mask(pci_dev, false);
    } else {
        ioapic_set_irq_mask(pci_read_irq_line(pci_dev), false);
    }
}

static uint16_t e1000_device_ids[] = {
    0x1004, 0x100c, 0x100e, 0x100f,
    0x1010, 0x1011, 0x1012, 0x1013, 0x1015, 0x1016, 0x1017, 0x1018, 0x1019, 0x101a, 0x101d,
    0x1026, 0x1027, 0x1028,
    0x1076, 0x1078, 0x1079, 0x107a, 0x107b,
    0x10d3, 0x10ea, 0x1107, 0x1112,
    0x1209,
    0x1502, 0x1539, 0x153a, 0x15bc,
    0x1a1c,
};

struct pci_driver e1000_driver = {
    .init = e1000_init,
    .name = "e1000",
    .match_condition = PCI_DRIVER_MATCH_VENDOR_AND_DEVICE_ID,
    .match_data = {
        .device_count = SIZEOF_ARRAY(e1000_device_ids),
        .device_ids = e1000_device_ids,
        .vendor_id = 0x8086,
    },
};
