#include "hw/pci/pci.h"
//#define LOGLEVEL LOGLEVEL_DEBUG
#include "caos/log.h"

#include "hw/pci/virtio_blk.h"

static int mmio_out(struct pci_handler *pHdl,uint32_t address,int bar,int datalen,void *pData) {

	LOG("Write access.. Addr=0x%x",address);
	return 0;
}

static int mmio_in(struct pci_handler *pHdl,uint32_t address,int bar,int datalen,void *pData) {

	LOG("Read access.. Addr=0x%x",address);
	return 0;
}


int hw_pci_virtio_blk_init(void) {
	static pci_handler_t handler;

	handler.cfgspace.decoded.vid      = 0x1AF4;
	handler.cfgspace.decoded.pid      = 0x1001; // Block
	handler.cfgspace.decoded.class    = 0x1;    // Mass storage
	handler.cfgspace.decoded.subclass = 0x80;   // "Other"
	handler.cfgspace.decoded.progif   = 0;

	handler.bar[0].size = 0x4000;

	handler.mmio_out = mmio_out;
	handler.mmio_in  = mmio_in;

	hw_pci_register_handler(&handler);

	return 0;
}