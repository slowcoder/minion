#include <string.h>
#include <stdlib.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_net.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_config.h>
#include "vm/vm_internal.h"
#include "hw/pci/pci.h"
#include "hw/disks/disks.h"
//#define LOGLEVEL LOGLEVEL_DEBUG
#include "caos/log.h"

#include "hw/pci/virtio_net.h"

#define MAX_QUEUES 2
#define QUEUE_NUM 0x40

typedef struct {
	struct vm   *pVM;

	uint32_t gm_qpfn[MAX_QUEUES];

	uint16_t curr_queue;

	uint8_t  pci_status;
	int      iir; // Interrupt indication
} ctx_t;

static const char *statusString[] = {"Acknowledged","Driver","DriverOK","FeaturesOK"};

#if 0
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
#endif 

static void on_queue_notify(ctx_t *pCtx,uint16_t arg) {

	ASSERT(0,"Implement me, maybe?");

}

static int mmio_out(struct pci_handler *pHdl,uint32_t address,int bar,int datalen,void *pData) {
	uint32_t val;
	ctx_t *pCtx = (ctx_t*)pHdl->opaque;

	LOGD("Write access.. Addr=%u, Len=%i, Data[0]=0x%02x",address,datalen,*(uint8_t*)pData);

	if( datalen == 1 ) val = *(uint8_t*)pData;
	else if( datalen == 2 ) val = *(uint16_t*)pData;
	else if( datalen == 4 ) val = *(uint32_t*)pData;
	else ASSERT(0,"Implement datalen=%i",datalen); 

	switch(address) {
		case VIRTIO_PCI_GUEST_FEATURES: // Off 4, 32-bit
			LOGD("Features activated by guest: 0x%x",val);
			break;
		case VIRTIO_PCI_QUEUE_SEL: // Off 14, 16-bit
			ASSERT(val<2,"Only two queues allowed with virtio_net (val=%u)",val);
			pCtx->curr_queue = val;
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
			LOGD("Queue %u PFN set to 0x%x",pCtx->curr_queue,val);
			pCtx->gm_qpfn[pCtx->curr_queue] = val << 12;
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

	LOGD("Read access.. Addr=%u, Len=%i",address,datalen);

	switch(address) {
		case VIRTIO_PCI_HOST_FEATURES: // Off 0, 32-bit
			 *(uint32_t*)pData = 0; // No features..
			break;
		case VIRTIO_PCI_QUEUE_NUM: // Off 12, 16-bit
			*(uint16_t*)pData = QUEUE_NUM; // Entries, not bytes
			break;
		case VIRTIO_PCI_QUEUE_SEL: // Off 14, 16-bit
			*(uint16_t*)pData = pCtx->curr_queue;
			break;
		case VIRTIO_PCI_STATUS: // Off 18, 8-bit
			 *(uint8_t*)pData = pCtx->pci_status;
			 break;
		case VIRTIO_PCI_QUEUE_PFN: // Off 8, 32-bit
			*(uint32_t*)pData = pCtx->gm_qpfn[pCtx->curr_queue] >> 12;
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
		// Since we're not doing MSI-X, 20 and up is virtio_net specific
		default:
			ASSERT(0,"Read access.. Addr=%u, Len=%i",address,datalen);
	}

	return 0;
}

int hw_pci_virtio_net_init(struct vm *pVM) {
	static pci_handler_t handler;
	ctx_t *pCtx;

	handler.cfgspace.decoded.vid      = 0x1AF4;
	handler.cfgspace.decoded.pid      = 0x1000; // Net
	handler.cfgspace.decoded.class    = 0x2;    // Network
	handler.cfgspace.decoded.subclass = 0x00;   // Ethernet
	handler.cfgspace.decoded.progif   = 0;
	handler.cfgspace.decoded.command  = 2 | 1; // MEM + IO

	handler.cfgspace.decoded.subsys_id  = 1;
	handler.cfgspace.decoded.subsys_vid = 0x1AF4;

	handler.cfgspace.decoded.irqline    = 15; // PIC IRQ #15
	handler.cfgspace.decoded.irqpin     = 1; // INTA#

	handler.bar[0].size = 0x4000;

	handler.mmio_out = mmio_out;
	handler.mmio_in  = mmio_in;

	pCtx = (ctx_t*)calloc(1,sizeof(ctx_t));
	pCtx->pVM = pVM;

	handler.opaque = pCtx;

	hw_pci_register_handler(&handler);

	return 0;
}

int hw_pci_virtio_net_destroy(struct vm *pVM) {
	return 0;
}
