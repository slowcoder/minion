#pragma once

struct vm;

int hw_pci_virtio_net_init(struct vm *pVM);
int hw_pci_virtio_net_destroy(struct vm *pVM);
