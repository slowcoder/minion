#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>
#include <sys/mman.h>
#include <asm/bootparam.h>
#include <assert.h>

//#define LOGLEVEL LOGLEVEL_DEBUG
#include "caos/log.h"

#include "vm_internal.h"

static const char *BZIMAGE_MAGIC = "HdrS";

typedef enum {
	eImagetype_Invalid = 0,
	eImagetype_Linux32,
	eImagetype_Linux64,
} eImagetype;

typedef struct kernelsection {
	uint64_t paddr;
	uint32_t size;
	uint8_t *pData;
	uint8_t *pFreeable;
} kernelsection_t;

static int load_bzImage(vm_t *pVM,const char *pzFilename) {
	struct boot_params boot;
	FILE *in;
	ssize_t setup_size,file_size,sr;
	void *pPmode = NULL;

	LOG("Loading \"%s\" as bzImage",pzFilename);

	in = fopen(pzFilename,"rb");
	if( in == NULL ) {
		LOGE("Failed to open file \"%s\"",pzFilename);
		return -1;
	}

	if( fread(&boot,1,sizeof(boot),in) != sizeof(boot))
		return -2;

	if (memcmp(&boot.hdr.header, BZIMAGE_MAGIC, strlen(BZIMAGE_MAGIC)))
		return -3;

	LOGD(" * Magic matches");
	if( boot.hdr.xloadflags & XLF_KERNEL_64 ) {
		LOGD(" * 64bit kernel");
		//pLoader->type = eImagetype_Linux64;
	} else {
		LOGD(" * 32bit kernel");
		//pLoader->type = eImagetype_Linux32;
		ASSERT(0,"TODO: Support 32bit kernels");
	}
	//LOG("xLoadFlags: 0x%x",boot.hdr.xloadflags);
	LOGD(" * Setup sectors: %u",boot.hdr.setup_sects);
	ASSERT(boot.hdr.setup_sects != 0,"Kernel too old to load");

	setup_size = (boot.hdr.setup_sects + 1) << 9;

	fseek(in, 0, SEEK_END);
	file_size = ftell(in);
	fseek(in, 0, SEEK_SET);

	// Allocate and read the protected mode part (TODO: mmap)
	pPmode = calloc(1,file_size+1);
	sr = fread(pPmode,1,file_size,in);
	assert(sr == file_size);

	LOGD(" * Read %lu bytes of kernel",sr);
	fclose(in);

	pVM->kernel.numsections = 1;

	pVM->kernel.section = (kernelsection_t*)calloc(pVM->kernel.numsections,sizeof(kernelsection_t));

	pVM->kernel.section[0].paddr = 0x100000UL;
	pVM->kernel.section[0].size  = sr - setup_size;
	pVM->kernel.section[0].pData = (uint8_t*)pPmode + setup_size;
	pVM->kernel.section[0].pFreeable = pPmode;
	pVM->kernel.entrypoint = 0x100000UL;

	return 0;
}

static int create_zeropage(vm_t *pVM) {
	struct boot_params *zero_page = NULL;
	struct setup_header *kernel_header = NULL;
	char *cmd_line = NULL;

	zero_page = (struct boot_params*)intvm_memory_getguestspaceptr(pVM,0x7E00);
	cmd_line  = (char *)intvm_memory_getguestspaceptr(pVM,0x20000);	

	strcpy(cmd_line,"noapic noacpi pci=conf1 i8042.direct=1 console=ttyS0 i8042.noaux=1 earlyprintk=serial");
	strcpy(cmd_line,"nolapic noapic noacpi pci=conf1 reboot=k panic=1 i8042.direct=1 i8042.dumbkbd=1 i8042.nopnp=1 earlyprintk=serial console=ttyS0 i8042.noaux=1 root=ram0 rw");
///	strcpy(cmd_line,"apic=debug pci=conf1 reboot=k panic=1 i8042.direct=1 i8042.dumbkbd=1 i8042.nopnp=1 earlyprintk=serial console=ttyS0 i8042.noaux=1 root=/dev/vda rw");

	// Zero out ourselves
	memset(zero_page, 0, sizeof(struct boot_params));

	// Populate with the setup_header from the loaded kernel
	kernel_header = (struct setup_header*)((uint8_t*)pVM->kernel.section[0].pFreeable + 0x1F1);
	memcpy(&zero_page->hdr,
		kernel_header,
		kernel_header->setup_sects * 512);

	// TODO: Check the "version" of the bootparams

	zero_page->hdr.type_of_loader = 0xFF;
	zero_page->hdr.cmd_line_ptr   = 0x20000;
	zero_page->hdr.cmdline_size   = strlen(cmd_line) + 1;

	zero_page->e820_entries++;
	zero_page->e820_map[0].addr = 0x00000000;
	zero_page->e820_map[0].size = 0x0009fc00; // Top of 640KB
	zero_page->e820_map[0].type = E820_RAM;

	LOGD("Signaling %u MB RAM to VM",pVM->config.ramsize);

//	if( pVM->pLoader->type == eImagetype_Linux64 ) {

	zero_page->e820_entries++;
	zero_page->e820_map[1].addr = 0x100000UL; // A20 range and up to 3GB
	zero_page->e820_map[1].size = (pVM->config.ramsize-1) * 1024 * 1024;
	zero_page->e820_map[1].type = E820_RAM;

	// Reserve 3G->4G for MMIO
	zero_page->e820_entries++;
	zero_page->e820_map[2].addr = 0xC0000000UL; // 3GB
	zero_page->e820_map[2].size = 0xC0000000UL; // 1GB
	zero_page->e820_map[2].type = E820_UNUSABLE;

#if 0
	} else if( pVM->pLoader->type == eImagetype_Linux32 ) {
		// 32bit needs to have _some_ free address-space
		// at the top of the address-space to map in MMIO devices
		zero_page->e820_entries++;
		zero_page->e820_map[1].addr =  0x100000UL; // A20 range and up to 4GB
		zero_page->e820_map[1].size =  (pVM->mem.totmegs-1) * 1024 * 1024;
		zero_page->e820_map[1].type = E820_RAM;
	}
#endif
	return 0;
}

//#define CR0_PE (1<<0)

static void fill_segment_descriptor(uint64_t *dt, struct kvm_segment *seg)
{
	uint16_t index = seg->selector >> 3;
	uint32_t limit = seg->g ? seg->limit >> 12 : seg->limit;

	dt[index] = (limit & 0xffff) /* Limit bits 0:15 */
		| (seg->base & 0xffffff) << 16 /* Base bits 0:23 */
		| (uint64_t)seg->type << 40
		| (uint64_t)seg->s << 44 /* system or code/data */
		| (uint64_t)seg->dpl << 45 /* Privilege level */
		| (uint64_t)seg->present << 47
		| (limit & 0xf0000ULL) << 48 /* Limit bits 16:19 */
		| (uint64_t)seg->avl << 52 /* Available for system software */
		| (uint64_t)seg->l << 53 /* 64-bit code segment */
		| (uint64_t)seg->db << 54 /* 16/32-bit segment */
		| (uint64_t)seg->g << 55 /* 4KB granularity */
		| (seg->base & 0xff000000ULL) << 56; /* Base bits 24:31 */
}
/* Builds a GDT at 0x1000 (guest) */
static int build_gdt(vm_t *pVM) {
	static struct kvm_segment seg;
	uint64_t *gdt;

	seg.base = 0;
	seg.limit = 0xffffffff;
	seg.present = 1;
	seg.dpl = 0;
	seg.db = 1;
	seg.s = 1; /* Code/data */
	seg.l = 0;
	seg.g = 1; /* 4KB granularity */

	gdt = (uint64_t *)intvm_memory_getguestspaceptr(pVM,0x1000);
	/* gdt[0] is the null segment */

	seg.type = 11; /* Code: execute, read, accessed */
	seg.selector = 0x10;
	fill_segment_descriptor(gdt, &seg);

	seg.type = 3; /* Data: read/write, accessed */
	seg.selector = 0x18;
	fill_segment_descriptor(gdt, &seg);

	return 0;
}

static int copy_kernel_to_guest(vm_t *pVM) {
	memcpy( intvm_memory_getguestspaceptr(pVM,pVM->kernel.section[0].paddr),
			pVM->kernel.section[0].pData,
			pVM->kernel.section[0].size );

	return 0;
}

int intvm_setup_gpregs(vm_t *pVM,uint64_t rip,uint64_t rsi);

int intvm_loader_loadkernel(vm_t *pVM,const char *pzFilename) {

	if( load_bzImage(pVM,pzFilename) != 0 ) {
		LOGE("Failed to load kernel-image");
		return -1;
	}
	if( create_zeropage(pVM) != 0 ) {
		LOGE("Failed to create zero-page");
		return -1;
	}
	if( copy_kernel_to_guest(pVM) != 0 ) {
		LOGE("Failed to copy kernel");
		return -1;
	}
	if( build_gdt(pVM) != 0 ) {
		LOGE("Failed to build GDT");
		return -1;
	}

	if( intvm_setup_gpregs(pVM,pVM->kernel.entrypoint,0x7E00) != 0 ) {
		LOGE("Failed to setup GP registers");
		return -1;		
	}

	return 0;
}

int intvm_loader_release(vm_t *pVM) {
	int i,nsect;

	nsect = pVM->kernel.numsections;
	for(i=0;i<nsect;i++) {
		if( pVM->kernel.section[i].pFreeable != NULL ) {
			free(pVM->kernel.section[i].pFreeable);
			pVM->kernel.section[i].pFreeable = NULL;
		}
	}
	return 0;
}
