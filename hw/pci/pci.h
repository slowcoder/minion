#pragma once

#include <stdint.h>

typedef struct pci_handler {
  void     *opaque;
} pci_handler_t;

int hw_pci_init(void);
int hw_pci_register_handler(pci_handler_t *handler);

int hw_pci_mmio_out(uint64_t addr,int datalen,void *pData);
int hw_pci_mmio_in(uint64_t addr,int datalen,void *pData);
