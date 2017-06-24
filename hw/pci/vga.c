#include "hw/isa/isa.h"
#include "hw/pci/pci.h"

#include "hw/pci/serial.h"

static uint8_t vga_inb(struct isa_handler *hdl,uint16_t port) {
	return 0xFF;
}
static void  vga_outb(struct isa_handler *hdl,uint16_t port,uint8_t val) {
}
static void  vga_outw(struct isa_handler *hdl,uint16_t port,uint16_t val) {
}

int hw_pci_vga_init(void) {
	static pci_handler_t pci_hdl;

	pci_hdl.cfgspace.decoded.vid      = 0x1234;
	pci_hdl.cfgspace.decoded.pid      = 0x5679;
	pci_hdl.cfgspace.decoded.class    = 0x3; // Display Controllers
	pci_hdl.cfgspace.decoded.subclass = 0;   // VGA
	pci_hdl.cfgspace.decoded.progif   = 0;   // VGA
	hw_pci_register_handler(&pci_hdl);

	// We're going to need the VGA registers as well
	static struct isa_handler isa_hdl[2];

	isa_hdl[0].base   = 0x03C0;
	isa_hdl[0].mask   = ~0xF;
	isa_hdl[0].inb  = vga_inb;
	isa_hdl[0].outb = vga_outb;
	hw_isa_register_handler(&isa_hdl[0]);

	isa_hdl[1].base   = 0x03D0;
	isa_hdl[1].mask   = ~0xF;
	isa_hdl[1].inb  = vga_inb;
	isa_hdl[1].outb = vga_outb;
	isa_hdl[1].outw = vga_outw;
	hw_isa_register_handler(&isa_hdl[1]);


	return 0;
}