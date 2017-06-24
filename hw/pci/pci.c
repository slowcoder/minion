#include <string.h>
//#define LOGLEVEL LOGLEVEL_DEBUG
#include "caos/log.h"

#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_internal.h"

#define ADDRESS_PORT 0xCF8
#define DATA_PORT    0xCFC

#define MAX_PCIDEVS 8

typedef struct {
	int bInUse;
	pcidev_t       dev;
	pci_handler_t *pHandler;
} devhandler_t;
static devhandler_t devHandler[MAX_PCIDEVS] = { 0 };

static busaddress_t curr_addr = { .raw = 0 };

static pcidev_t *getDev(busaddress_t ba) {
	int i;

	for(i=0;i<MAX_PCIDEVS;i++) {
		if( devHandler[i].bInUse ) {
			if( (devHandler[i].dev.ba.raw & 0x7FFFFF00) == (curr_addr.raw & 0x7FFFFF00) ) {
				return &devHandler[i].dev;
			}
		}
	}

	return NULL;
}

static void write_address(uint32_t raw) {
	curr_addr.raw = raw & ~3;

	LOGD("Address: %02x:%02x:%1x reg=0x%02x en=%i null=%x (raw=0x%08x)",
		curr_addr.bus,
		curr_addr.dev,
		curr_addr.func,
		curr_addr.reg,
		curr_addr.enable,
		curr_addr.raw & 3,
		curr_addr.raw);
}

static int write_bar(pcidev_t *pDev,int bar,uint32_t val) {

	pDev->cfgspace.decoded.bar[bar] = val;

	pDev->bar[bar].base = val & ~3;

	LOGD("%p: BAR%i = 0x%08x",pDev,bar,val);

	return 0;
}

static uint32 read_bar(pcidev_t *pDev,int bar) {

	LOGD("%p: BAR%i = 0x%08x",pDev,bar,pDev->cfgspace.decoded.bar[bar]);

	if( pDev->cfgspace.decoded.bar[bar] == 0xFFFFFFFF ) { // Sizing?
		return ~( pDev->bar[bar].size - 1 );
	} else {
		return pDev->cfgspace.decoded.bar[bar];
	}
}

static int write_device_config_b(pcidev_t *pDev,int reg,uint32_t val) {
	if( reg == 0xC ) // BIST
		return 0;

	if( (reg < 0x10) || (reg >= 0x28) ) {
		LOGW("Write to non-BAR register (0x%02x, val=0x%x). Not supported",reg,val);
		return 0;
	}

	return 0;
}

static int write_device_config_l(pcidev_t *pDev,int reg,uint32_t val) {
	int bar;

	if( (reg < 0x10) || (reg >= 0x28) ) {
		LOGW("Write to non-BAR register (0x%02x). Not supported",reg);
		return 0;
	}

	bar = (reg - 0x10) / 4;
	write_bar(pDev,bar,val);

	return 0;
}

static int read_device_config(pcidev_t *pDev,int reg,int len,void *pRet) {
	if( pDev == NULL ) return -1;

	LOGD("Read from reg 0x%x (0x%x masked)",reg,reg&~3);
	reg &= ~3;
	switch(reg) {
		case 0x00: // Device+Vendor ID
			memcpy(pRet,pDev->cfgspace.raw,len);
			break;
		case 0x04: // Command+Status
			LOGD("Command/Status=0x%08x (%x:%02x.%x)",
				*(uint32_t*)&pDev->cfgspace.raw[4],
				pDev->ba.bus,
				pDev->ba.dev,
				pDev->ba.func);
		case 0x08: //  Class, subclas, ProgIF, RevID
		case 0x0C: //  BIST, HeaderType, Latency, CacheLineSize
		case 0x28: //  Cardbus CIS pointer
		case 0x2C: //  Subsystem ID, Subsystem Vendor ID
		case 0x30: //  Expansion ROM base address
		case 0x34:
		case 0x38:
		case 0x3C: //  MaxLatency, MinGrant,InterruptPin,InterruptLine
			memcpy(pRet,pDev->cfgspace.raw + reg,len);
			break;
		case 0x10: //  BAR0
		case 0x14: //  BAR1
		case 0x18: //  BAR2
		case 0x1C: //  BAR3
		case 0x20: //  BAR4
		case 0x24: //  BAR5
			ASSERT(len==4,"Short-read of BAR?");
			*(uint32_t*)pRet = read_bar(pDev,(reg-0x10)/4);
			break;
		default:
			ASSERT(0,"Unhandled register 0x%02x",reg);
			break;
	}

	return 0;
}

static uint32_t isa_pci_inl(struct isa_handler *hdl,uint16_t port);

static uint8_t isa_pci_inb(struct isa_handler *hdl,uint16_t port) {
	if( port >= DATA_PORT ) {
		uint32_t v;

		v = isa_pci_inl(hdl,port&~3);
		v >>= (port - DATA_PORT) * 8;

		return v & 0xFFFF;
	} else {
		ASSERT(port == DATA_PORT,"Reading from non-data port? (0x%04x)",port);
	}

	return ~0;
}

static uint16_t isa_pci_inw(struct isa_handler *hdl,uint16_t port) {
#if 1
//	pcidev_t *pDev;

	if( port >= DATA_PORT ) {
		uint32_t v;

		v = isa_pci_inl(hdl,port&~3);
		v >>= (port - DATA_PORT) * 8;

		return v & 0xFFFF;
	} else {
		ASSERT(port == DATA_PORT,"Reading from non-data port? (0x%04x)",port);
	}
#else
	ASSERT(0,"Implement me?");
#endif
	return ~0;
}

static uint32_t isa_pci_inl(struct isa_handler *hdl,uint16_t port) {
	pcidev_t *pDev;

	if( port == ADDRESS_PORT ) {
		return curr_addr.raw;
	} else if( port == DATA_PORT ) {
		pDev = getDev(curr_addr);
		if( pDev != NULL ) {
			uint32_t val;

			read_device_config(pDev,curr_addr.reg,4,&val);
			LOGD("Reg 0x%02x = 0x%08x",curr_addr.reg,val);
			return val;
		}		
	} else {
		ASSERT(port == DATA_PORT,"Reading from non-data port? (0x%04x)",port);
	}


	return ~0;
}

static void  isa_pci_outb(struct isa_handler *hdl,uint16_t port,uint8_t val) {
	pcidev_t *pDev;

	if( port < DATA_PORT ) { // I.e, address-port
		uint32_t newaddr;
		int shift;

		shift = (port - ADDRESS_PORT) * 8;
		shift = 24 - shift;

		newaddr = curr_addr.raw & ~(0xFF<<shift);
		newaddr |= val << shift;
		write_address(newaddr);
		return;
	} else if( port >= DATA_PORT ) {
		pDev = getDev(curr_addr);
		if( pDev != NULL ) {
			write_device_config_b(pDev,curr_addr.reg,val);
			return;
		} else {
			return; // Write to data-port of non-existent device?
		}
	}
	ASSERT(0,"Unhandled write to 0x%04x (Val=0x%x)",port,val);
}

static void  isa_pci_outw(struct isa_handler *hdl,uint16_t port,uint16_t val) {
	pcidev_t *pDev;

	if( port == DATA_PORT ) {
		pDev = getDev(curr_addr);
		if( pDev != NULL ) {
			if( curr_addr.reg == 0x4 ) { // Command
				LOGD("Setting Command=0x%04x",val);
				pDev->cfgspace.decoded.command = val & 0xFFFF;
				return; // HACK!
			}
		}
	}
	LOG("Unhandled word-write.. Port=0x%04x, Val=0x%04x",port,val);

	LOG("Address: %02x:%02x:%1x reg=0x%02x en=%i null=%x (raw=0x%08x)",
		curr_addr.bus,
		curr_addr.dev,
		curr_addr.func,
		curr_addr.reg,
		curr_addr.enable,
		curr_addr.raw & 3,
		curr_addr.raw);

	ASSERT(0,"Foo..");
}

static void  isa_pci_outl(struct isa_handler *hdl,uint16_t port,uint32_t val) {
	pcidev_t *pDev;

	if( port == ADDRESS_PORT ) {
		ASSERT(val&0x80000000,"Not a configuration-cycle write..");
		write_address(val);
	} else if( port == DATA_PORT ) {
		pDev = getDev(curr_addr);
		if( pDev != NULL ) {
			write_device_config_l(pDev,curr_addr.reg,val);
//			LOGD("Reg 0x%02x = 0x%08x",curr_addr.reg,val);
//			return val;
		}
	} else {
		ASSERT(0,"Unhandled write to 0x%04x (Val=0x%x)",port,val);
	}
}

int hw_pci_init(void) {
	static struct isa_handler hdl;

	hdl.base   = 0x0CF8;
	hdl.mask   = ~7;
	hdl.inb    = isa_pci_inb;
	hdl.inw    = isa_pci_inw;
	hdl.inl    = isa_pci_inl;
	hdl.outb   = isa_pci_outb;
	hdl.outw   = isa_pci_outw;
	hdl.outl   = isa_pci_outl;
	hdl.opaque = NULL;
	hw_isa_register_handler(&hdl);

	return 0;
}

static int iNumDevOnBus = 0; 
int hw_pci_register_handler(pci_handler_t *handler) {
	int i,bar;

	for(i=0;i<MAX_PCIDEVS;i++) {
		if( !devHandler[i].bInUse ) {
			// Assign a PCI address (Always bus0)
			devHandler[i].dev.ba.bus  = 0;
			devHandler[i].dev.ba.dev  = iNumDevOnBus++;
			devHandler[i].dev.ba.func = 0;

			devHandler[i].pHandler = handler;

			// Copy to "our" cfgspace
			memcpy(&devHandler[i].dev.cfgspace,&handler->cfgspace,sizeof(pci_configspace_t));

			// Copy to "our" BARs
			memcpy(&devHandler[i].dev.bar,&handler->bar,sizeof(pci_bar_t) * 6);

			// Pre-populate the raw BARs
			for(bar=0;bar<6;bar++) {
				devHandler[i].dev.cfgspace.decoded.bar[bar] = devHandler[i].dev.bar[bar].base | !!(devHandler[i].dev.bar[bar].bIsIO);
			}

			devHandler[i].bInUse = 1;

			return 0;
		}
	}

	return -1;
}

static int getDevByMMIO(uint32_t addr,devhandler_t **ppDH,int *piBar) {
	int i,b;

	for(i=0;i<MAX_PCIDEVS;i++) {
		if( devHandler[i].bInUse ) {
			for(b=0;b<6;b++) { // Check all BARs
				pci_bar_t *pBar;

				pBar = &devHandler[i].dev.bar[b];
				if( (addr >= pBar->base) && (addr < (pBar->base + pBar->size)) ) {
					*ppDH  = &devHandler[i];
					*piBar = b;
					return 0;
				} 
			}
		}
	}
	return -1;
}

int hw_pci_mmio_out(uint64_t addr,int datalen,void *pData) {
	devhandler_t *pDH;
	int bar;

	if( getDevByMMIO(addr,&pDH,&bar) != 0 ) {
		return -1;
	}

	return pDH->pHandler->mmio_out(pDH->pHandler,
		addr - pDH->dev.bar[bar].base,
		bar,
		datalen,
		pData);
}

int hw_pci_mmio_in(uint64_t addr,int datalen,void *pData) {
	devhandler_t *pDH;
	int bar;

	if( getDevByMMIO(addr,&pDH,&bar) != 0 ) {
		return -1;
	}

	return pDH->pHandler->mmio_in(pDH->pHandler,
		addr - pDH->dev.bar[bar].base,
		bar,
		datalen,
		pData);
}
