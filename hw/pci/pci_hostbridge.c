#include "hw/pci/pci.h"

#include "hw/pci/pci_hostbridge.h"

int hw_pci_hostbridge_init(void) {
	static pci_handler_t handler;

	handler.cfgspace.decoded.vid      = 0x8086;
	handler.cfgspace.decoded.pid      = 0x0001;
	handler.cfgspace.decoded.class    = 0x6; // Bridge device
	handler.cfgspace.decoded.subclass = 0;   // Host bridge
	handler.cfgspace.decoded.progif   = 0;

	hw_pci_register_handler(&handler);

	return 0;
}