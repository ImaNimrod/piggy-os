#ifndef _AHCI_DEFINITIONS_H
#define _AHCI_DEFINITIONS_H 1

#include <dev/block/ata.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/spinlock.h>
#include <utils/vector.h>

#define BOHC_BOS    (1 << 0)
#define BOHC_OOS    (1 << 1)
#define BOHC_BB     (1 << 4)

#define CAP_SSS     (1 << 27)
#define CAP_S64A    (1 << 31)

#define GHC_HR      (1 << 0)
#define GHC_IE      (1 << 1)
#define GHC_AE      (1 << 31)

#define FIS_TYPE_REG_H2D 0x27

#define HBA_PxCMD_ST            (1 << 0)
#define HBA_PxCMD_SUD           (1 << 1)
#define HBA_PxCMD_FRE           (1 << 4)
#define HBA_PxCMD_FR            (1 << 14)
#define HBA_PxCMD_CR            (1 << 15)

#define HBA_PxIE_DHRE       (1 << 0)
#define HBA_PxIE_PSE        (1 << 1)
#define HBA_PxIE_DSE        (1 << 2)
#define HBA_PxIE_SDBE       (1 << 3)
#define HBA_PxIE_DPE        (1 << 5)
#define HBA_PxIE_ERROR_MASK 0x7dc00050

#define HBA_PxSIG_ATA   0x00000101
#define	HBA_PxSIG_ATAPI 0xeb140101

#define HBA_PxTFD_DRQ       (1 << 3)
#define HBA_PxTFD_BSY       (1 << 7)

struct hba_port {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t : 32;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t devslp;
    uint32_t reserved[11];
    uint32_t vs[10];
} __attribute__((packed));

struct hba_registers {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is; 
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_ports;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t bohc;
    uint32_t cap2;
    uint32_t reserved[29];
    uint32_t vendor[24];
    struct hba_port ports[];
} __attribute__((packed));

struct hba_command_header {
    uint8_t cfl: 5;
    uint8_t a: 1;
    uint8_t w: 1;
    uint8_t p: 1;

    uint8_t r: 1;
    uint8_t b: 1;
    uint8_t c: 1;
    uint8_t : 1;		// Reserved
    uint8_t pmp: 4;

    uint16_t prdtl;
    uint32_t prdbc;

    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed));

struct hba_prdt {
    uint32_t dba;
    uint32_t dbau;
    uint32_t : 32;
    uint32_t dbc: 22;
    uint32_t : 9;
    uint32_t i: 1;
} __attribute__((packed));

struct hba_command_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct hba_prdt prdt[8];
} __attribute__((packed));

struct hba_fis_h2d {
    uint8_t type;
    uint8_t pmport: 4;
    uint8_t : 3;
    uint8_t c: 1;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint16_t count;
    uint8_t icc;
    uint8_t control;
    uint32_t : 32;
} __attribute__((packed));

struct ahci_controller {
    struct hba_registers* hba_registers;
    uint8_t port_count;
    uint8_t slot_count;
    vector_t* devices;
};

struct ahci_device {
    struct ahci_controller* controller;

    uint8_t port_number;
    struct hba_port* hba_port;
    uintptr_t clb_and_fis_paddr;
    uintptr_t command_table_paddr;

    ata_device_type_t type;
    char serial_number[ATA_IDENTIFY_SERIAL_SIZE + 1];
    char firmware_revision[ATA_IDENTIFY_FIRMWARE_SIZE + 1];
    char model_number[ATA_IDENTIFY_MODEL_SIZE + 1];
    size_t sector_count;
    size_t sector_size;

    spinlock_t lock;
};

bool send_command(struct ahci_device* device, uint8_t command, uintptr_t paddr, uint64_t lba, uint16_t block_count, bool write);

void ahci_device_try_init(struct ahci_controller* controller, uint8_t port_number, struct hba_port* hba_port);
void ahci_device_irq_handler(struct ahci_device* device);

#endif /* _AHCI_DEFINITIONS_H */
