#pragma once

#include <stdint.h>

typedef struct pcibar {
	uint32_t base;
	uint32_t size;
	int bIsIO;
} pci_bar_t;

typedef union pci_configspace {
	struct {
		uint16_t vid,pid;
		uint16_t command,status;
		uint8_t  revid,progif,subclass,class;
		uint8_t  clsz,lat,hdrtype,bist;
		uint32_t bar[6];
	} decoded;
	uint8_t  raw[256];
} pci_configspace_t;

typedef struct pci_handler {
  void     *opaque;

  pci_bar_t bar[6];

  pci_configspace_t cfgspace;

  int (*mmio_out)(struct pci_handler *,uint32_t address,int bar,int datalen,void *pData);
  int (*mmio_in )(struct pci_handler *,uint32_t address,int bar,int datalen,void *pData);
} pci_handler_t;

int hw_pci_init(void);
int hw_pci_register_handler(pci_handler_t *handler);

int hw_pci_mmio_out(uint64_t addr,int datalen,void *pData);
int hw_pci_mmio_in(uint64_t addr,int datalen,void *pData);
