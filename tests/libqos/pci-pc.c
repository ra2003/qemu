/*
 * libqos PCI bindings for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci-pc.h"
#include "qapi/qmp/qdict.h"
#include "hw/pci/pci_regs.h"

#include "qemu-common.h"


#define ACPI_PCIHP_ADDR         0xae00
#define PCI_EJ_BASE             0x0008
#define PCI_SEL_BASE            0x0010

#define PCI_BRIDGE_CLASS            0x06
#define PCI_TO_PCI_BRIDGE_SUBCLASS  0x04

#define PCI_BRIDGE_PRIMARY_BUS_OFFSET       0x18
#define PCI_BRIDGE_SECONDARY_BUS_OFFSET     0x19
#define PCI_BRIDGE_SUBORDINATE_BUS_OFFSET   0x1A

typedef struct QPCIBusPC
{
    QPCIBus bus;
} QPCIBusPC;

static uint8_t qpci_pc_pio_readb(QPCIBus *bus, uint32_t addr)
{
    return qtest_inb(bus->qts, addr);
}

static void qpci_pc_pio_writeb(QPCIBus *bus, uint32_t addr, uint8_t val)
{
    qtest_outb(bus->qts, addr, val);
}

static uint16_t qpci_pc_pio_readw(QPCIBus *bus, uint32_t addr)
{
    return qtest_inw(bus->qts, addr);
}

static void qpci_pc_pio_writew(QPCIBus *bus, uint32_t addr, uint16_t val)
{
    qtest_outw(bus->qts, addr, val);
}

static uint32_t qpci_pc_pio_readl(QPCIBus *bus, uint32_t addr)
{
    return qtest_inl(bus->qts, addr);
}

static void qpci_pc_pio_writel(QPCIBus *bus, uint32_t addr, uint32_t val)
{
    qtest_outl(bus->qts, addr, val);
}

static uint64_t qpci_pc_pio_readq(QPCIBus *bus, uint32_t addr)
{
    return (uint64_t)qtest_inl(bus->qts, addr) +
           ((uint64_t)qtest_inl(bus->qts, addr + 4) << 32);
}

static void qpci_pc_pio_writeq(QPCIBus *bus, uint32_t addr, uint64_t val)
{
    qtest_outl(bus->qts, addr, val & 0xffffffff);
    qtest_outl(bus->qts, addr + 4, val >> 32);
}

static void qpci_pc_memread(QPCIBus *bus, uint32_t addr, void *buf, size_t len)
{
    memread(addr, buf, len);
}

static void qpci_pc_memwrite(QPCIBus *bus, uint32_t addr,
                             const void *buf, size_t len)
{
    memwrite(addr, buf, len);
}

static inline void qpci_pc_config_set_addr(QPCIBus *bus, int devfn,
                                           uint8_t offset)
{
    uint32_t addr = (uint32_t)(0x80000000) |
        (((uint32_t)bus->id & 0xFF) << 16) |
        (((uint32_t)devfn & 0xFF) << 8) |
        (offset);

    qtest_outl(bus->qts, 0xcf8, addr);
}

static uint8_t qpci_pc_config_readb(QPCIBus *bus, int devfn, uint8_t offset)
{
    qpci_pc_config_set_addr(bus, devfn, offset);
    return qtest_inb(bus->qts, 0xcfc);
}

static uint16_t qpci_pc_config_readw(QPCIBus *bus, int devfn, uint8_t offset)
{
    qpci_pc_config_set_addr(bus, devfn, offset);
    return qtest_inw(bus->qts, 0xcfc);
}

static uint32_t qpci_pc_config_readl(QPCIBus *bus, int devfn, uint8_t offset)
{
    qpci_pc_config_set_addr(bus, devfn, offset);
    return qtest_inl(bus->qts, 0xcfc);
}

static void qpci_pc_config_writeb(QPCIBus *bus, int devfn, uint8_t offset,
                                  uint8_t value)
{
    qpci_pc_config_set_addr(bus, devfn, offset);
    qtest_outb(bus->qts, 0xcfc, value);
}

static void qpci_pc_config_writew(QPCIBus *bus, int devfn, uint8_t offset,
                                  uint16_t value)
{
    qpci_pc_config_set_addr(bus, devfn, offset);
    qtest_outw(bus->qts, 0xcfc, value);
}

static void qpci_pc_config_writel(QPCIBus *bus, int devfn, uint8_t offset,
                                  uint32_t value)
{
    qpci_pc_config_set_addr(bus, devfn, offset);
    qtest_outl(bus->qts, 0xcfc, value);
}

static QPCIBus *qpci_create_bus(QTestState *qts, uint16_t id);

static void qpci_topology_scan_cb(QPCIDevice *dev, int fn, void *data)
{
    uint16_t class_id = qpci_config_readw(dev, PCI_CLASS_DEVICE);
    if (class_id == ((PCI_BRIDGE_CLASS << 8) | PCI_TO_PCI_BRIDGE_SUBCLASS)) {

        QPCIBus *parent = dev->bus;

        /* Handle PCI-to-PCI bridge device */
        QPCIBus *child = qpci_create_bus(parent->qts,
                                         parent->total_subordinates + 1);
        QLIST_INSERT_HEAD(&parent->children, child, link);

        parent->total_subordinates = child->total_subordinates + 1;
        if (parent->subordinate_bus_id < child->subordinate_bus_id) {
            parent->subordinate_bus_id = child->subordinate_bus_id;
        }

        qpci_config_writeb(dev, PCI_BRIDGE_PRIMARY_BUS_OFFSET, parent->id);
        qpci_config_writeb(dev, PCI_BRIDGE_SECONDARY_BUS_OFFSET, child->id);
        qpci_config_writeb(dev, PCI_BRIDGE_SUBORDINATE_BUS_OFFSET,
                           child->subordinate_bus_id);
        qpci_device_enable(dev);
    }

    g_free(dev);
}

static QPCIBus *qpci_create_bus(QTestState *qts, uint16_t id)
{
    QPCIBusPC *ret = g_new0(QPCIBusPC, 1);

    assert(qts);

    ret->bus.pio_readb = qpci_pc_pio_readb;
    ret->bus.pio_readw = qpci_pc_pio_readw;
    ret->bus.pio_readl = qpci_pc_pio_readl;
    ret->bus.pio_readq = qpci_pc_pio_readq;

    ret->bus.pio_writeb = qpci_pc_pio_writeb;
    ret->bus.pio_writew = qpci_pc_pio_writew;
    ret->bus.pio_writel = qpci_pc_pio_writel;
    ret->bus.pio_writeq = qpci_pc_pio_writeq;

    ret->bus.memread = qpci_pc_memread;
    ret->bus.memwrite = qpci_pc_memwrite;

    ret->bus.config_readb = qpci_pc_config_readb;
    ret->bus.config_readw = qpci_pc_config_readw;
    ret->bus.config_readl = qpci_pc_config_readl;

    ret->bus.config_writeb = qpci_pc_config_writeb;
    ret->bus.config_writew = qpci_pc_config_writew;
    ret->bus.config_writel = qpci_pc_config_writel;

    ret->bus.qts = qts;
    ret->bus.pio_alloc_ptr = 0xc000 + id * 0x1000;
    ret->bus.mmio_alloc_ptr = 0xE0000000 + id * 0x1000;
    ret->bus.mmio_limit = 0x100000000ULL;

    ret->bus.id = id;
    ret->bus.total_subordinates = 0;
    ret->bus.subordinate_bus_id = id; /* For now we're our own subordinate */
    QLIST_INIT(&ret->bus.children);

    qpci_device_foreach(&ret->bus, -1, -1, qpci_topology_scan_cb, ret);

    return &ret->bus;
}

QPCIBus *qpci_init_pc(QTestState *qts, QGuestAllocator *alloc)
{
    QPCIBus *root = qpci_create_bus(qts, 0);
    return root;
}

void qpci_free_pc(QPCIBus *bus)
{
    if (!bus) {
        return;
    }

    while (!QLIST_EMPTY(&bus->children)) {
        QPCIBus *child = QLIST_FIRST(&bus->children);
        g_assert(child);

        QLIST_REMOVE(child, link);
        qpci_free_pc(child);
    }

    QPCIBusPC *s = container_of(bus, QPCIBusPC, bus);
    g_free(s);
}

void qpci_unplug_acpi_device_test(const char *id, uint8_t slot)
{
    qpci_unplug_acpi_device_bus_test(id, 0, slot);
}

void qpci_unplug_acpi_device_bus_test(const char *id, uint8_t bus, uint8_t slot)
{
    QDict *response;
    char *cmd;

    cmd = g_strdup_printf("{'execute': 'device_del',"
                          " 'arguments': {"
                          "   'id': '%s'"
                          "}}", id);
    response = qmp(cmd);
    g_free(cmd);
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    qtest_outb(global_qtest, ACPI_PCIHP_ADDR + PCI_SEL_BASE, bus);
    qtest_outb(global_qtest, ACPI_PCIHP_ADDR + PCI_EJ_BASE, 1 << slot);

    qmp_eventwait("DEVICE_DELETED");
}
