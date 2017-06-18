#include <stdint.h>
#include "hw/isa/isa.h"
#include "hw/isa/cmos.h"
#include "hw/isa/i8042.h"
#include "hw/isa/i8250.h"
#include "hw/pci/pci.h"

#include "hw/devices.h"

int devices_init(struct vm *pVM) {

	hw_isa_init();
	hw_isa_i8250_init(pVM);
	hw_isa_cmos_init();
	hw_isa_i8042_init();

	hw_pci_init();

	return 0;
}

int devices_io_out(uint16_t port,int datalen,void *pData) {

	if( hw_isa_io_out(port,datalen,pData) == 0 ) {
		return 0;
	}
	return -1;
}

int devices_io_in(uint16_t port,int datalen,void *pData) {
	if( hw_isa_io_in(port,datalen,pData) == 0 ) {
		return 0;
	}
	return -1;
}

int devices_mmio_out(uint64_t addr,int datalen,void *pData) {
	int r;

	r = hw_pci_mmio_out(addr,datalen,pData);

	return r;
}

int devices_mmio_in(uint64_t addr,int datalen,void *pData) {
	int r;

	r = hw_pci_mmio_in(addr,datalen,pData);

	return r;
}
