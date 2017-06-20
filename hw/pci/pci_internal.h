#pragma once

#include "hw/pci/pci.h"

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

	pci_bar_t bar[6];

#if 1
	pci_configspace_t cfgspace;
#else
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
#endif
} pcidev_t;
#pragma pack(pop)
