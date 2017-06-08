#include <stdlib.h>

#include "caos/log.h"

#include "hw/io.h"
#include "hw/pci.h"

static struct {
  int configspace;
  uint8 bus,dev,fn,index;
} state;

static uint16_t pci_inw(struct io_handler *hdl,uint16_t port) {
	//ASSERT(0,"Fixme - Port = 0x%04x",hdl->base + port);
	return ~0; // No device here..
}

static uint32_t pci_inl(struct io_handler *hdl,uint16_t port) {
	if(port == 0) {
		uint32 ret;

		ret  = state.configspace << 31;
		ret |= (state.bus & 0x0F) << 16;
		ret |= (state.dev & 0x1F) << 11;
		ret |= state.index & 0xFF;

		LOG("PCI Conf Readback (cs=0x%x)",state.configspace);
		return ret;
	} else {
		return ~0;
	}

	return ~0; // No device here..
}

static void  pci_outb(struct io_handler *hdl,uint16_t port,uint8_t val) {
	LOG("PCI write @ %u (val=0x%x)",port,val);
}

static void  pci_outw(struct io_handler *hdl,uint16_t port,uint16_t val) {
	ASSERT(0,"Unimplemented");
}

static void  pci_outl(struct io_handler *hdl,uint16_t port,uint32_t val) {
	LOG("PCI write @ %u (val=0x%x)",port,val);

	if( port == 0 ) { // Address
		state.configspace = val>>31;
		state.index = val & 0xFF;
		state.bus   = (val >> 16) & 0x0F;
		state.dev   = (val >> 11) & 0x1F;
		state.fn    = (val >>  8) & 0x07;
	} else {
		ASSERT(0,"Unimplemented");
	}
}

int pci_init(void) {
	static struct io_handler hdl;

	hdl.base   = 0x0CF8;
	//hdl.mask   = 0xFFFB;
	hdl.mask   = ~7;
	//hdl.inb    = pci_inb;
	hdl.inw    = pci_inw;
	hdl.inl    = pci_inl;
	hdl.outb   = pci_outb;
	hdl.outw   = pci_outw;
	hdl.outl   = pci_outl;
	hdl.opaque = NULL;
	io_register_handler(&hdl);

	return 0;
}