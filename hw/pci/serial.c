#include "hw/pci/pci.h"

#include "hw/pci/serial.h"

#if 0
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
} pci_handler_t;
#endif

int hw_pci_serial_init(void) {
	static pci_handler_t handler;

	handler.cfgspace.decoded.vid      = 0x1234;
	handler.cfgspace.decoded.pid      = 0x5678;
	handler.cfgspace.decoded.class    = 0x7; // Simple Communication Controllers
	handler.cfgspace.decoded.subclass = 0;   // Serial
	handler.cfgspace.decoded.progif   = 0;   // Generic XT-Compatible Serial Controller
//	handler.cfgspace.decoded.bar[0] = 0x3F8 | 1;

	handler.bar[0].base  = 0x3F8;
	handler.bar[0].size  = 0x7;
	handler.bar[0].bIsIO = 1;

	hw_pci_register_handler(&handler);

	return 0;
}