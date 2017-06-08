#include "hw/io.h"
#include "hw/pci.h"
#include "hw/cmos.h"
#include "hw/i8250.h"
#include "hw/i8042.h"

#include "hw/devices.h"

int devices_init(void) {

	io_init();

	pci_init();
	cmos_init();
	i8250_init();
	i8042_init();

	return 0;
}
