#include <string.h>
#include <stdlib.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_blk.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_config.h>
#include "vm/vm_internal.h"
#include "hw/pci/pci.h"
#include "hw/disks/disks.h"
//#define LOGLEVEL LOGLEVEL_VERB
#include "caos/log.h"

#include "hw/pci/virtio_blk.h"

#define QUEUE_NUM 0x40

typedef struct {
	struct vm   *pVM;
	struct disk *pDisk;

	uint32_t gm_pfn;
	uint8_t  pci_status;
	int      iir; // Interrupt indication
} ctx_t;

static const char *statusString[] = {"Acknowledged","Driver","DriverOK","FeaturesOK"};

static int get_vring(ctx_t *pCtx,struct vring *pRing) {
	int numdesc;
	uint64_t gm_desc,gm_avail,gm_used; // GuestMemory addresses

	ASSERT(pRing != NULL,"NULL is not a valid pointer");

	// This is the only address we know. The rest needs to be figured out..
	gm_desc = pCtx->gm_pfn;
	LOGV("Desc : 0x%llx",gm_desc);
	numdesc = QUEUE_NUM;

	// Available follows the descriptors
	gm_avail = gm_desc + sizeof(struct vring_desc) * numdesc;
	LOGV("Avail: 0x%llx",gm_avail);

	// used is aligned to a page boundary
	gm_used  = gm_avail + 2 + 2 + 2 * numdesc;
	if( gm_used & 0xFFF ) gm_used = (gm_used & ~0xFFF) + 0x1000; // Align to 4K
	LOGV("Used : 0x%llx",gm_used);

	pRing->num   = numdesc;
	pRing->desc  = (struct vring_desc *)intvm_memory_getguestspaceptr(pCtx->pVM,gm_desc);
	pRing->avail = (struct vring_avail*)intvm_memory_getguestspaceptr(pCtx->pVM,gm_avail);
	pRing->used  = (struct vring_used *)intvm_memory_getguestspaceptr(pCtx->pVM,gm_used);

	return 0;
}

typedef struct {
	struct virtio_blk_outhdr *pCmd;
	uint8_t *pStatus;
	uint32_t num_blocks;
	void    *pData;
} ioreq_t;

static uint16_t shadow_used = 0;
static uint16_t curr_op = 0;

static int fullfill_ioreq(ctx_t *pCtx,struct vring *pVring,ioreq_t *pReq,int data_idx) {
	uint64_t addr,size;
	int r;

	addr = pReq->pCmd->sector * 512ULL;
	size = pReq->num_blocks * 512ULL;

	if( pReq->pCmd->type == 0 ) { // Read
		r = disks_read(pCtx->pDisk,addr,size,pReq->pData);
	} else if( pReq->pCmd->type == 1 ) { // Write
		r = disks_write(pCtx->pDisk,addr,size,pReq->pData);
	} else {
		ASSERT(0,"Unhandled command type 0x%x",pReq->pCmd->type);
	}

	ASSERT(r==0,"Handle IO errors");

	*pReq->pStatus = VIRTIO_BLK_S_OK;

	LOGV("Setting used->ring[%i].id to %i",curr_op,data_idx - 1);
	pVring->used->ring[curr_op].id = data_idx - 1;
	pVring->used->ring[curr_op].len = 4096;
	curr_op++;

	shadow_used++;
	pVring->used->idx = shadow_used;
	return 0;
}

static void on_queue_notify(ctx_t *pCtx,uint16_t arg) {
	struct vring vring;
	int buffer_descndx = -1;
	int i;
	ioreq_t ioreq;
	int irq_flag;

	ASSERT(arg == 0x00,"Only one queue supported");

	if( get_vring(pCtx,&vring) != 0 ) {
		LOGE("Failed to locate vring");
		return;
	}

	LOGV("VRing - Num: %u (0x%x)",vring.num,vring.num);
	LOGV("Used  - idx=0x%x",vring.used->idx);
	LOGV("Avail - idx=0x%x",vring.avail->idx);

	memset(&ioreq,0,sizeof(ioreq));
	irq_flag = 0;

	int todo;

	todo = vring.avail->idx - vring.used->idx;
	LOGV("Items todo: %i",todo);

	for(i=0;i<todo*3;i++) {
		if( vring.desc[i].len > 0 ) LOGD("Desc %i - Len = %u",i,vring.desc[i].len);

		if( vring.desc[i].len == sizeof(struct virtio_blk_outhdr) ) { // The request
			ioreq.pCmd = (struct virtio_blk_outhdr *)intvm_memory_getguestspaceptr(pCtx->pVM,vring.desc[i].addr);
		} else if( vring.desc[i].len == 1 ) { // Status
			ioreq.pStatus = (uint8_t *)intvm_memory_getguestspaceptr(pCtx->pVM,vring.desc[i].addr);
		} else if( vring.desc[i].len >= 0x200 ) { // Data buffer
			ioreq.num_blocks = vring.desc[i].len >> 9;
			ioreq.pData = (uint8_t *)intvm_memory_getguestspaceptr(pCtx->pVM,vring.desc[i].addr);
			buffer_descndx = i;
		}

		// See if we have a complete iorequest
		if( (ioreq.pData != NULL) && (ioreq.pStatus != NULL) && (ioreq.pCmd != NULL) ) {

			LOGV("At desc %i, have a full ioreq (Type=%i,Len=%u)",i,ioreq.pCmd->type,ioreq.num_blocks<<9);

			fullfill_ioreq(pCtx,&vring,&ioreq,i-1);
			irq_flag = 1;

			(void)buffer_descndx;

			// Reset the struct
			memset(&ioreq,0,sizeof(ioreq));
		}
	}

	if( irq_flag ) {
		//vring.used->idx++;
		LOGV("About to IRQ. Used=0x%x (desc[0].len=%u, .addr=%llx)",vring.used->idx,vring.desc[0].len,vring.desc[0].addr);
		pCtx->iir = 1;
		intvm_irq_set(7,1);
		curr_op = 0;
	}
}

static int mmio_out(struct pci_handler *pHdl,uint32_t address,int bar,int datalen,void *pData) {
	uint32_t val;
	ctx_t *pCtx = (ctx_t*)pHdl->opaque;

	LOGD("Write access.. Addr=0x%x, Len=%i, Data[0]=0x%02x",address,datalen,*(uint8_t*)pData);

	if( datalen == 1 ) val = *(uint8_t*)pData;
	else if( datalen == 2 ) val = *(uint16_t*)pData;
	else if( datalen == 4 ) val = *(uint32_t*)pData;
	else ASSERT(0,"Implement datalen=%i",datalen); 

	switch(address) {
		case VIRTIO_PCI_GUEST_FEATURES: // Off 4, 32-bit
			LOGD("Features activated by guest: 0x%x",val);
			break;
		case VIRTIO_PCI_QUEUE_SEL: // Off 14, 16-bit
			ASSERT(val==0,"Only one queue allowed with virtio_blk");
			break;
		case VIRTIO_PCI_STATUS: // Off 18, 8-bit
			if( val & VIRTIO_CONFIG_S_FAILED ) LOG("Status: FAILED");
			else {
				int b;
				for(b=0;b<4;b++) {
					if( val & (1<<b) ) LOGD("Status: %s",statusString[b]);
				}
			}
			pCtx->pci_status = val;
			break;
		case VIRTIO_PCI_QUEUE_PFN: // Off 8, 32-bit
			LOGD("Queue PFN set to 0x%x",val);
			pCtx->gm_pfn = val << 12;
			break;
		case VIRTIO_PCI_QUEUE_NOTIFY: // Off 16, 16-bit
			LOGD("Queue notify!  Val=0x%x",val);

			on_queue_notify(pCtx,val);
			break;
		// Since we're not doing MSI-X, 20 and up is virtio_blk specific
		default:
			ASSERT(0,"Write access.. Addr=%u, Len=%i, Data[0]=0x%02x",address,datalen,*(uint8_t*)pData);
	}

	return 0;
}

static int mmio_in(struct pci_handler *pHdl,uint32_t address,int bar,int datalen,void *pData) {
	ctx_t *pCtx = (ctx_t*)pHdl->opaque;
	uint64_t capacity;

	LOGD("Read access.. Addr=%u, Len=%i",address,datalen);

	disks_get_capacity(pCtx->pDisk,&capacity);
	capacity /= 512ULL;

	switch(address) {
		case VIRTIO_PCI_HOST_FEATURES: // Off 0, 32-bit
			 *(uint32_t*)pData = 0; // No features..
			break;
		case VIRTIO_PCI_QUEUE_NUM: // Off 12, 16-bit
			*(uint16_t*)pData = QUEUE_NUM; // Entries, not bytes
			break;
		case VIRTIO_PCI_QUEUE_SEL: // Off 14, 16-bit
			*(uint16_t*)pData = 0;
			break;
		case VIRTIO_PCI_STATUS: // Off 18, 8-bit
			 *(uint8_t*)pData = pCtx->pci_status;
			 break;
		case VIRTIO_PCI_QUEUE_PFN: // Off 8, 32-bit
			*(uint32_t*)pData = 0x80001000 >> 12;
			*(uint32_t*)pData = pCtx->gm_pfn >> 12;
			break;
		case VIRTIO_PCI_QUEUE_NOTIFY: // Off 16, 16-bit
			ASSERT(0,"Huh?");
			break;
		case VIRTIO_PCI_ISR: // Off 19, 8-bit
			*(uint8_t*)pData = pCtx->iir;
			// Reading PCI_ISR also acknowledges the IRQ
			intvm_irq_set(7,0);
			pCtx->iir = 0;
			break;
		// Since we're not doing MSI-X, 20 and up is virtio_blk specific
		case 20: // Capacity, LSB
		case 21:
		case 22:
		case 23:
		case 24:
		case 25:
		case 26:
		case 27: // Capacity, MSB
			{
				uint8_t t;
				t = (capacity >> (8*(address-20))) & 0xFF;
				*(uint8_t*)pData = t;
				break;
			}
			break;
		default:
			ASSERT(0,"Read access.. Addr=%u, Len=%i",address,datalen);
	}

	return 0;
}

static ctx_t *pWorkaround = NULL;
int hw_pci_virtio_blk_init(struct vm *pVM) {
	static pci_handler_t handler;
	ctx_t *pCtx;

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

	handler.bar[0].size = 0x4000;

	handler.mmio_out = mmio_out;
	handler.mmio_in  = mmio_in;

	pCtx = (ctx_t*)calloc(1,sizeof(ctx_t));
	pCtx->pVM = pVM;

	handler.opaque = pCtx;
	pWorkaround = pCtx;

	hw_pci_register_handler(&handler);

	return 0;
}

int hw_pci_virtio_blk_attachdisk(struct vm *pVM,struct disk *pDisk) {

	pWorkaround->pDisk = pDisk;

	return -1;
}
