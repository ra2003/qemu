/*
 * QTest testcase for vga cards
 *
 * Copyright (c) 2014 Red Hat, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

static void pci_cirrus(void)
{
    qtest_start("-vga none -device cirrus-vga");
    qtest_end();
}

static void pci_stdvga(void)
{
    qtest_start("-vga none -device VGA");
    qtest_end();
}

static void pci_secondary(void)
{
    qtest_start("-vga none -device secondary-vga");
    qtest_end();
}

static void pci_multihead(void)
{
    qtest_start("-vga none -device VGA -device secondary-vga");
    qtest_end();
}

static void pci_virtio_gpu(void)
{
    qtest_start("-vga none -device virtio-gpu-pci");
    qtest_end();
}

static void pci_virtio_vga(void)
{
    qtest_start("-vga none -device virtio-vga");
    qtest_end();
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "alpha") == 0 || strcmp(arch, "i386") == 0 ||
        strcmp(arch, "mips") == 0 || strcmp(arch, "x86_64") == 0) {

        if (qtest_is_device_supported("cirrus-vga")) {
            qtest_add_func("/display/pci/cirrus", pci_cirrus);
        }
    }

    /* stdvga and secondary vga are enabled by CONFIG_VGA which is
     * used in tests/Makefile.include to ignore this test completely */
    qtest_add_func("/display/pci/stdvga", pci_stdvga);
    qtest_add_func("/display/pci/secondary", pci_secondary);
    qtest_add_func("/display/pci/multihead", pci_multihead);

    if (qtest_is_device_supported("virtio-gpu-pci")) {
        qtest_add_func("/display/pci/virtio-gpu", pci_virtio_gpu);
    }

    if (qtest_is_device_supported("virtio-vga")) {
        qtest_add_func("/display/pci/virtio-vga", pci_virtio_vga);
    }

    return g_test_run();
}
