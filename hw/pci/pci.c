#include <string.h>
#include "caos/log.h"

#include "hw/isa/isa.h"
#include "hw/pci/pci.h"

#define ADDRESS_PORT 0xCF8
#define DATA_PORT    0xCFC

#pragma pack(push,1)
typedef struct {
	union {
		struct {
			uint32_t reg  : 8;
			uint32_t func : 3;
			uint32_t dev  : 5;
			uint32_t bus  : 8;
			uint32_t reserved : 7;
			uint32_t enable   : 1;
		};
		uint32_t raw;
	};
} busaddress_t;

typedef struct {
	busaddress_t ba;

	union {
		struct {
			uint16_t vid,pid;
			uint16_t command,status;
			uint8_t  revid,progif,subclass,class;
			uint8_t  clsz,lat,hdrtype,bist;
			uint32_t bar[6];
		} configspace;
		uint8_t  configspacebacking[256];
	};
} pcidev_t;
#pragma pack(pop)

static pcidev_t fakedev[2] = {

	{ // Fake host-bridge device
		.ba.bus = 0,
		.ba.dev = 0,
		.ba.func = 0,
		.configspace.vid = 0x8086,
		.configspace.pid = 0x0001,
		.configspace.class = 0x6, // Bridge device
		.configspace.subclass = 0, // Host bridge
		.configspace.progif = 0,
	},
	{ // Fake serial port
		.ba.bus = 0,
		.ba.dev = 1,
		.ba.func = 0,
		.configspace.vid = 0x1234,
		.configspace.pid = 0x5678,
		.configspace.class = 0x7,  // Simple Communication Controllers
		.configspace.subclass = 0, // Serial
		.configspace.progif = 0,   // Generic XT-Compatible Serial Controller
		.configspace.bar[0] = 0x3F8 | 1,
	}
};

static busaddress_t curr_addr = { .raw = 0 };

static pcidev_t *getDev(busaddress_t ba) {
	int i;

	for(i=0;i<(sizeof(fakedev)/sizeof(pcidev_t));i++) {
		if( (fakedev[i].ba.raw & 0x7FFFFF00) == (curr_addr.raw & 0x7FFFFF00) ) {
			return &fakedev[i];
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

	if( (pDev->ba.bus == 0) && (pDev->ba.dev == 1) && (bar==0) ) {
		if( val == 0xFFFFFFFF ) { // Get size
			pDev->configspace.bar[bar] = 0xFFFFFFF8;
		} else {
			pDev->configspace.bar[bar] = val;
		}
	}

	LOGD("%p: BAR%i = 0x%08x",pDev,bar,val);

	return 0;
}

static uint32 read_bar(pcidev_t *pDev,int bar) {

	LOGD("%p: BAR%i = 0x%08x",pDev,bar,pDev->configspace.bar[bar]);

	return pDev->configspace.bar[bar];
}

static int write_device_config_b(pcidev_t *pDev,int reg,uint32_t val) {
	if( (reg < 0x10) || (reg >= 0x28) ) {
		LOGW("Write to non-BAR register (0x%02x). Not supported",reg);
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
			memcpy(pRet,pDev->configspacebacking,len);
			break;
		case 0x04: // Command+Status
			LOGD("Command/Status=0x%08x (%x:%02x.%x)",
				*(uint32_t*)&pDev->configspacebacking[4],
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
			memcpy(pRet,pDev->configspacebacking + reg,len);
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
		LOG("Shift=%i - Val=0x%02x",shift,val);
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
				LOG("Setting Command=0x%04x",val);
				pDev->configspace.command = val & 0xFFFF;
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

int hw_pci_register_handler(pci_handler_t *handler) {
	return 0;
}
