#pragma once

#include <stdint.h>

typedef struct pci_handler {
  void     *opaque;
} pci_handler_t;

int hw_pci_init(void);
int hw_pci_register_handler(pci_handler_t *handler);

