#include <string.h>
#include "caos/log.h"

#include "hw/isa/isa.h"
#include "hw/pci/pci.h"

static uint16_t isa_pci_inw(struct isa_handler *hdl,uint16_t port) {
	return ~0;
}
static uint32_t isa_pci_inl(struct isa_handler *hdl,uint16_t port) {
	return ~0;
}

static void  isa_pci_outb(struct isa_handler *hdl,uint16_t port,uint8_t val) {
}
static void  isa_pci_outl(struct isa_handler *hdl,uint16_t port,uint32_t val) {
}

int hw_pci_init(void) {
	static struct isa_handler hdl;

	hdl.base   = 0x0CF8;
	hdl.mask   = ~7;
	hdl.inb    = NULL;
	hdl.inw    = isa_pci_inw;
	hdl.inl    = isa_pci_inl;
	hdl.outb   = isa_pci_outb;
	hdl.outw   = NULL;
	hdl.outl   = isa_pci_outl;
	hdl.opaque = NULL;
	hw_isa_register_handler(&hdl);

	return 0;
}

int hw_pci_register_handler(pci_handler_t *handler) {
	return 0;
}
