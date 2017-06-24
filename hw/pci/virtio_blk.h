#pragma once

struct vm;

int hw_pci_virtio_blk_init(struct vm *pVM);
int hw_pci_virtio_blk_destroy(struct vm *pVM);

struct disk;
int hw_pci_virtio_blk_attachdisk(struct vm *pVM,struct disk *pDisk);
