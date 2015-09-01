/*
 * vfio based device assignment support - PCI devices
 *
 * Copyright Red Hat, Inc. 2012-2015
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#ifndef HW_VFIO_VFIO_PCI_H
#define HW_VFIO_VFIO_PCI_H

#include "qemu-common.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/vfio/vfio-common.h"
#include "qemu/event_notifier.h"
#include "qemu/queue.h"
#include "qemu/timer.h"

struct VFIOPCIDevice;

typedef struct VFIOQuirk {
    MemoryRegion mem;
    struct VFIOPCIDevice *vdev;
    QLIST_ENTRY(VFIOQuirk) next;
    struct {
        uint32_t base_offset:TARGET_PAGE_BITS;
        uint32_t address_offset:TARGET_PAGE_BITS;
        uint32_t address_size:3;
        uint32_t bar:3;

        uint32_t address_match;
        uint32_t address_mask;

        uint32_t address_val:TARGET_PAGE_BITS;
        uint32_t data_offset:TARGET_PAGE_BITS;
        uint32_t data_size:3;

        uint8_t flags;
        uint8_t read_flags;
        uint8_t write_flags;
    } data;
} VFIOQuirk;

typedef struct VFIOBAR {
    VFIORegion region;
    bool ioport;
    bool mem64;
    QLIST_HEAD(, VFIOQuirk) quirks;
} VFIOBAR;

typedef struct VFIOVGARegion {
    MemoryRegion mem;
    off_t offset;
    int nr;
    QLIST_HEAD(, VFIOQuirk) quirks;
} VFIOVGARegion;

typedef struct VFIOVGA {
    off_t fd_offset;
    int fd;
    VFIOVGARegion region[QEMU_PCI_VGA_NUM_REGIONS];
} VFIOVGA;

typedef struct VFIOINTx {
    bool pending; /* interrupt pending */
    bool kvm_accel; /* set when QEMU bypass through KVM enabled */
    uint8_t pin; /* which pin to pull for qemu_set_irq */
    EventNotifier interrupt; /* eventfd triggered on interrupt */
    EventNotifier unmask; /* eventfd for unmask on QEMU bypass */
    PCIINTxRoute route; /* routing info for QEMU bypass */
    uint32_t mmap_timeout; /* delay to re-enable mmaps after interrupt */
    QEMUTimer *mmap_timer; /* enable mmaps after periods w/o interrupts */
} VFIOINTx;

typedef struct VFIOMSIVector {
    /*
     * Two interrupt paths are configured per vector.  The first, is only used
     * for interrupts injected via QEMU.  This is typically the non-accel path,
     * but may also be used when we want QEMU to handle masking and pending
     * bits.  The KVM path bypasses QEMU and is therefore higher performance,
     * but requires masking at the device.  virq is used to track the MSI route
     * through KVM, thus kvm_interrupt is only available when virq is set to a
     * valid (>= 0) value.
     */
    EventNotifier interrupt;
    EventNotifier kvm_interrupt;
    struct VFIOPCIDevice *vdev; /* back pointer to device */
    int virq;
    bool use;
} VFIOMSIVector;

enum {
    VFIO_INT_NONE = 0,
    VFIO_INT_INTx = 1,
    VFIO_INT_MSI  = 2,
    VFIO_INT_MSIX = 3,
};

/* Cache of MSI-X setup plus extra mmap and memory region for split BAR map */
typedef struct VFIOMSIXInfo {
    uint8_t table_bar;
    uint8_t pba_bar;
    uint16_t entries;
    uint32_t table_offset;
    uint32_t pba_offset;
    MemoryRegion mmap_mem;
    void *mmap;
} VFIOMSIXInfo;

typedef struct VFIOPCIDevice {
    PCIDevice pdev;
    VFIODevice vbasedev;
    VFIOINTx intx;
    unsigned int config_size;
    uint8_t *emulated_config_bits; /* QEMU emulated bits, little-endian */
    off_t config_offset; /* Offset of config space region within device fd */
    unsigned int rom_size;
    off_t rom_offset; /* Offset of ROM region within device fd */
    void *rom;
    int msi_cap_size;
    VFIOMSIVector *msi_vectors;
    VFIOMSIXInfo *msix;
    int nr_vectors; /* Number of MSI/MSIX vectors currently in use */
    int interrupt; /* Current interrupt type */
    VFIOBAR bars[PCI_NUM_REGIONS - 1]; /* No ROM */
    VFIOVGA vga; /* 0xa0000, 0x3b0, 0x3c0 */
    PCIHostDeviceAddress host;
    EventNotifier err_notifier;
    EventNotifier req_notifier;
    int (*resetfn)(struct VFIOPCIDevice *);
    uint32_t features;
#define VFIO_FEATURE_ENABLE_VGA_BIT 0
#define VFIO_FEATURE_ENABLE_VGA (1 << VFIO_FEATURE_ENABLE_VGA_BIT)
#define VFIO_FEATURE_ENABLE_REQ_BIT 1
#define VFIO_FEATURE_ENABLE_REQ (1 << VFIO_FEATURE_ENABLE_REQ_BIT)
    int32_t bootindex;
    uint8_t pm_cap;
    bool has_vga;
    bool pci_aer;
    bool req_enabled;
    bool has_flr;
    bool has_pm_reset;
    bool rom_read_failed;
    bool no_kvm_intx;
    bool no_kvm_msi;
    bool no_kvm_msix;
} VFIOPCIDevice;

typedef struct VFIORomBlacklistEntry {
    uint16_t vendor_id;
    uint16_t device_id;
} VFIORomBlacklistEntry;

uint32_t vfio_pci_read_config(PCIDevice *pdev, uint32_t addr, int len);
void vfio_pci_write_config(PCIDevice *pdev,
                           uint32_t addr, uint32_t val, int len);

uint64_t vfio_vga_read(void *opaque, hwaddr addr, unsigned size);
void vfio_vga_write(void *opaque, hwaddr addr, uint64_t data, unsigned size);

bool vfio_blacklist_opt_rom(VFIOPCIDevice *vdev);
void vfio_vga_quirk_setup(VFIOPCIDevice *vdev);
void vfio_vga_quirk_teardown(VFIOPCIDevice *vdev);
void vfio_vga_quirk_free(VFIOPCIDevice *vdev);
void vfio_bar_quirk_setup(VFIOPCIDevice *vdev, int nr);
void vfio_bar_quirk_teardown(VFIOPCIDevice *vdev, int nr);
void vfio_bar_quirk_free(VFIOPCIDevice *vdev, int nr);

#endif /* HW_VFIO_VFIO_PCI_H */
