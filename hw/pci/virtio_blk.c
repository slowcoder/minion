#include <string.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_blk.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_config.h>
#include "vm/vm_internal.h"
#include "hw/pci/pci.h"
//#define LOGLEVEL LOGLEVEL_DEBUG
#include "caos/log.h"

#include "hw/pci/virtio_blk.h"

#define BACKING_SIZE 0x4000

#define QUEUE_NUM 0x40

static uint8_t rawbacking[BACKING_SIZE];

static uint32_t legacy[32];

static const char *statusString[] = {"Acknowledged","Driver","DriverOK","FeaturesOK"};

static int get_vring(struct vm *pVM,struct vring *pRing) {
//	struct vring_desc *pDesc;
	int numdesc;
	uint64_t gm_desc,gm_avail,gm_used; // GuestMemory addresses

	ASSERT(pRing != NULL,"NULL is not a valid pointer");

	// This is the only address we know. The rest needs to be figured out..
	gm_desc = legacy[VIRTIO_PCI_QUEUE_PFN] << 12;
	LOGD("Desc : 0x%llx",gm_desc);
	numdesc = QUEUE_NUM;

	// Available follows the descriptors
	gm_avail = gm_desc + sizeof(struct vring_desc) * numdesc;
	LOGD("Avail: 0x%llx",gm_avail);

	// used is aligned to a page boundary
	gm_used  = gm_avail + 2 + 2 + 2 * numdesc;
	if( gm_used & 0xFFF ) gm_used = (gm_used & ~0xFFF) + 0x1000; // Align to 4K
	LOGD("Used : 0x%llx",gm_used);

	pRing->num   = numdesc;
	pRing->desc  = (struct vring_desc *)intvm_memory_getguestspaceptr(pVM,gm_desc);
	pRing->avail = (struct vring_avail*)intvm_memory_getguestspaceptr(pVM,gm_avail);
	pRing->used  = (struct vring_used *)intvm_memory_getguestspaceptr(pVM,gm_used);

	return 0;
}

typedef struct {
	struct virtio_blk_outhdr *pCmd;
	uint8_t *pStatus;
	uint32_t num_blocks;
	void    *pData;
} ioreq_t;

static uint16_t shadow_used = 0;

static int fullfill_read_ioreq(struct vring *pVring,ioreq_t *pReq,int data_idx) {

	ASSERT(pReq->pCmd->type == 0,"Only read is implemented");

#if 1 // Fake disk contains incrementing uint32s
	uint32_t c,o,*p;
	c = pReq->pCmd->sector * (512/4);
	p = (uint32_t*)pReq->pData;
	for(o=0;o<(pReq->num_blocks*512)/4;o++) {
		p[o] = c + o;
	}
#else // Fake disk is all zeroes
	memset(pReq->pData,0,pReq->num_blocks * 512);
#endif

	*pReq->pStatus = VIRTIO_BLK_S_OK;

	pVring->used->ring[shadow_used].id = data_idx - 1;
	pVring->used->ring[shadow_used].len = 4096;
	shadow_used++;
	shadow_used %= pVring->num;

	LOGD("Setting used->idx to 0x%x",shadow_used);
	pVring->used->idx = shadow_used;

	return 0;
}

static void on_queue_notify(struct vm *pVM,uint16_t arg) {
	struct vring vring;
	int buffer_descndx = -1;
	int i;
	ioreq_t ioreq;
	int irq_flag;

	ASSERT(arg == 0x00,"Only one queue supported");

	if( get_vring(pVM,&vring) != 0 ) {
		LOGE("Failed to locate vring");
		return;
	}

	LOGD("VRing:");
	LOGD(" Num : %u",vring.num);
	LOGD("Avail - idx=0x%x",vring.avail->idx);
	LOGD("Used  - idx=0x%x",vring.used->idx);

	memset(&ioreq,0,sizeof(ioreq));
	irq_flag = 0;

	for(i=0;i<vring.num;i++) {
		if( vring.desc[i].len > 0 ) LOGD("Desc %i - Len = %u",i,vring.desc[i].len);

		if( vring.desc[i].len == sizeof(struct virtio_blk_outhdr) ) { // The request
			ioreq.pCmd = (struct virtio_blk_outhdr *)intvm_memory_getguestspaceptr(pVM,vring.desc[i].addr);
		} else if( vring.desc[i].len == 1 ) { // Status
			ioreq.pStatus = (uint8_t *)intvm_memory_getguestspaceptr(pVM,vring.desc[i].addr);
		} else if( vring.desc[i].len >= 0x200 ) { // Data buffer
			ioreq.num_blocks = vring.desc[i].len >> 9;
			ioreq.pData = (uint8_t *)intvm_memory_getguestspaceptr(pVM,vring.desc[i].addr);
			buffer_descndx = i;
		}

		// See if we have a complete iorequest
		if( (ioreq.pData != NULL) && (ioreq.pStatus != NULL) && (ioreq.pCmd != NULL) ) {

			LOGD("At desc %i, have a full ioreq (pData=%p)",i,ioreq.pData);

			fullfill_read_ioreq(&vring,&ioreq,i-1);
			irq_flag = 1;

			(void)buffer_descndx;

			// Reset the struct
			memset(&ioreq,0,sizeof(ioreq));
		}
	}

	if( irq_flag ) {
		//vring.used->idx++;
		LOGD("About to IRQ. Used=0x%x",vring.used->idx);
		intvm_irq_set(7,1);
	}
}

static int mmio_out(struct pci_handler *pHdl,uint32_t address,int bar,int datalen,void *pData) {
	uint32_t val;

	LOGD("Write access.. Addr=0x%x, Len=%i, Data[0]=0x%02x",address,datalen,*(uint8_t*)pData);

	if( datalen == 1 ) val = *(uint8_t*)pData;
	else if( datalen == 2 ) val = *(uint16_t*)pData;
	else if( datalen == 4 ) val = *(uint32_t*)pData;
	else ASSERT(0,"Implement datalen=%i",datalen); 

	switch(address) {
		case VIRTIO_PCI_GUEST_FEATURES: // Off 4, 32-bit
			LOGD("Features activated by guest: 0x%x",val);
			legacy[VIRTIO_PCI_GUEST_FEATURES] = val;
			break;
		case VIRTIO_PCI_QUEUE_SEL: // Off 14, 16-bit
			LOGD("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXx Selected queue: %u",val);
			legacy[VIRTIO_PCI_QUEUE_SEL] = val;
			break;
		case VIRTIO_PCI_STATUS: // Off 18, 8-bit
			if( val & VIRTIO_CONFIG_S_FAILED ) LOG("Status: FAILED");
			else {
				int b;
				for(b=0;b<4;b++) {
					if( val & (1<<b) ) LOGD("Status: %s",statusString[b]);
				}
			}
			legacy[VIRTIO_PCI_STATUS] = val;
			break;
		case VIRTIO_PCI_QUEUE_PFN: // Off 8, 32-bit
			LOGD("Queue PFN set to 0x%x",val);
			legacy[VIRTIO_PCI_QUEUE_PFN] = val;
			break;
		case VIRTIO_PCI_QUEUE_NOTIFY: // Off 16, 16-bit
			LOGD("Queue notify!  Val=0x%x",val);

			on_queue_notify(pHdl->opaque,val);
			break;
		// Since we're not doing MSI-X, 20 and up is virtio_blk specific
		default:
			ASSERT(0,"Write access.. Addr=%u, Len=%i, Data[0]=0x%02x",address,datalen,*(uint8_t*)pData);
	}

	return 0;
}

static int mmio_in(struct pci_handler *pHdl,uint32_t address,int bar,int datalen,void *pData) {

	LOGD("Read access.. Addr=%u, Len=%i",address,datalen);

	switch(address) {
		case VIRTIO_PCI_HOST_FEATURES: // Off 0, 32-bit
			 *(uint32_t*)pData = 0; // No features..
			break;
		case VIRTIO_PCI_QUEUE_NUM: // Off 12, 16-bit
			*(uint16_t*)pData = QUEUE_NUM; // Entries, not bytes
			break;
		case VIRTIO_PCI_QUEUE_SEL: // Off 14, 16-bit
			*(uint16_t*)pData = legacy[VIRTIO_PCI_QUEUE_SEL];
			break;
		case VIRTIO_PCI_STATUS: // Off 18, 8-bit
			 *(uint8_t*)pData = legacy[VIRTIO_PCI_STATUS];
			 break;
		case VIRTIO_PCI_QUEUE_PFN: // Off 8, 32-bit
			*(uint32_t*)pData = 0x80001000 >> 12;
			*(uint32_t*)pData = 0;
			break;
		case VIRTIO_PCI_QUEUE_NOTIFY: // Off 16, 16-bit
			ASSERT(0,"Huh?");
			break;
		case VIRTIO_PCI_ISR: // Off 19, 8-bit
			// TODO! What value should be returned here?
			*(uint8_t*)pData = 1; // VIRTIO_IRQ_HIGH
			intvm_irq_set(7,0);
			break;
		// Since we're not doing MSI-X, 20 and up is virtio_blk specific
		case 20:
			*(uint8_t*)pData = 0x10;
			break;
		case 21:
		case 22:
			*(uint8_t*)pData = 0x10;
			break;
		case 23:
		case 24:
		case 25:
		case 26:
		case 27:
			*(uint8_t*)pData = 0;
			break;
		default:
			ASSERT(0,"Read access.. Addr=%u, Len=%i",address,datalen);
	}

	return 0;
}

#define PCI_CAP_ID_VNDR 0x9 // Vendor specific

static void build_structs(void) {
	struct virtio_pci_cap *pCap;

	memset(rawbacking,0,BACKING_SIZE);

	pCap = (struct virtio_pci_cap *)rawbacking;

	pCap->cap_vndr = PCI_CAP_ID_VNDR;
	pCap->bar = 0;
	pCap->cfg_type = VIRTIO_PCI_CAP_COMMON_CFG;
	pCap->offset = 0x100;
	pCap->length = sizeof(struct virtio_pci_common_cfg);
}

int hw_pci_virtio_blk_init(struct vm *pVM) {
	static pci_handler_t handler;

	handler.cfgspace.decoded.vid      = 0x1AF4;
	handler.cfgspace.decoded.pid      = 0x1001; // Block
	handler.cfgspace.decoded.class    = 0x1;    // Mass storage
	handler.cfgspace.decoded.subclass = 0x80;   // "Other"
	handler.cfgspace.decoded.progif   = 0;
	handler.cfgspace.decoded.command  = 2 | 1; // MEM + IO

	handler.cfgspace.decoded.subsys_id  = 2; // Blk
	handler.cfgspace.decoded.subsys_vid = 0x1AF4;

	handler.cfgspace.decoded.irqline    = 7; // PIC IRQ #7
	handler.cfgspace.decoded.irqpin     = 1; // INTA#

	handler.bar[0].size = BACKING_SIZE;

	handler.mmio_out = mmio_out;
	handler.mmio_in  = mmio_in;

	handler.opaque = pVM;

	hw_pci_register_handler(&handler);

	build_structs();

	return 0;
}