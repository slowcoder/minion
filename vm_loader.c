#if 1
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

#include "caos/log.h"

#include "vm_internal.h"

static const char *BZIMAGE_MAGIC = "HdrS";

typedef enum {
	eImagetype_Invalid = 0,
	eImagetype_Linux32,
	eImagetype_Linux64,
} eImagetype;

typedef struct vmloader {
	eImagetype type;

	struct {
		uint64_t paddr,size;
		void *pData;
		void *pFreeable;
	} section[4];
	void *pRawFile;
	uint64_t entrypoint;
} vmloader_t;

static int  vm_loader_load_bzImage(vmloader_t *pLoader,const char *pzFilename) {
	struct boot_params boot;
	FILE *in;
	ssize_t setup_size,file_size,sr;
	void *pPmode = NULL;

	LOG("Loading \"%s\" as bzImage",pzFilename);

	in = fopen(pzFilename,"rb");
	if( in == NULL )
		return -1;

	if( fread(&boot,1,sizeof(boot),in) != sizeof(boot))
		return -2;

	if (memcmp(&boot.hdr.header, BZIMAGE_MAGIC, strlen(BZIMAGE_MAGIC)))
		return -3;

	LOG(" * Magic matches");
	if( boot.hdr.xloadflags & XLF_KERNEL_64 ) {
		LOG(" * 64bit kernel");
		pLoader->type = eImagetype_Linux64;
	} else {
		LOG(" * 32bit kernel");
		pLoader->type = eImagetype_Linux32;
	}
	//LOG("xLoadFlags: 0x%x",boot.hdr.xloadflags);
	LOG(" * Setup sectors: %u",boot.hdr.setup_sects);
	ASSERT(boot.hdr.setup_sects != 0,"Kernel too old to load");

	setup_size = (boot.hdr.setup_sects + 1) << 9;

	fseek(in, 0, SEEK_END);
	file_size = ftell(in);
	fseek(in, 0, SEEK_SET);

	// Allocate and read the protected mode part (TODO: mmap)
	pPmode = calloc(1,file_size+1);
	sr = fread(pPmode,1,file_size,in);
	assert(sr == file_size);

	LOG(" * Read %lu bytes of kernel",sr);
	fclose(in);

	pLoader->section[0].paddr = 0x100000UL;
	pLoader->section[0].size  = sr - setup_size;
	pLoader->section[0].pData = (uint8_t*)pPmode + setup_size;
	pLoader->section[0].pFreeable = pPmode;
	pLoader->entrypoint = 0x100000UL;

	pLoader->pRawFile = pPmode;

	return 0;
}

static int vm_loader_create_bootparams(vm_t *pVM) {
	struct boot_params *zero_page = NULL;
	struct setup_header *kernel_header = NULL;
	char *cmd_line = NULL;

	zero_page = (struct boot_params*)( (uint8_t*)pVM->pLowRAM + 0x7E00 );
	cmd_line  = (char *)( (uint8_t*)pVM->pLowRAM + 0x20000 );	

	strcpy(cmd_line,"noapic noacpi pci=conf1 i8042.direct=1 console=ttyS0 i8042.noaux=1 earlyprintk=serial");
	strcpy(cmd_line,"noapic noacpi pci=conf1 reboot=k panic=1 i8042.direct=1 i8042.dumbkbd=1 i8042.nopnp=1 earlyprintk=serial console=ttyS0 i8042.noaux=1 root=/dev/vda rw");
//	strcpy(cmd_line,"apic=debug pci=conf1 reboot=k panic=1 i8042.direct=1 i8042.dumbkbd=1 i8042.nopnp=1 earlyprintk=serial console=ttyS0 i8042.noaux=1 root=/dev/vda rw");

	// Zero out ourselves
	memset(zero_page, 0, sizeof(struct boot_params));

	// Populate with the setup_header from the loaded kernel
	kernel_header = (struct setup_header*)((uint8_t*)pVM->pLoader->pRawFile + 0x1F1);
	memcpy(&zero_page->hdr,
		kernel_header,
		kernel_header->setup_sects * 512);

	// TODO: Check the "version" of the bootparams

	zero_page->hdr.type_of_loader = 0xFF;
	zero_page->hdr.cmd_line_ptr   = 0x20000;
	zero_page->hdr.cmdline_size   = strlen(cmd_line) + 1;

	zero_page->e820_entries++;
	zero_page->e820_map[0].addr =  0x00000000;
	zero_page->e820_map[0].size =  0x0009fc00; // Top of 640KB
	zero_page->e820_map[0].type = E820_RAM;

	LOG("Signaling %u MB RAM to VM",pVM->mem.totmegs);

	if( pVM->pLoader->type == eImagetype_Linux64 ) {
		zero_page->e820_entries++;
		zero_page->e820_map[1].addr =  0x100000UL; // A20 range and up to 4GB
		zero_page->e820_map[1].size =  (pVM->mem.totmegs-1) * 1024 * 1024;
		zero_page->e820_map[1].type = E820_RAM;
	} else if( pVM->pLoader->type == eImagetype_Linux32 ) {
		// 32bit needs to have _some_ free address-space
		// at the top of the address-space to map in MMIO devices
		zero_page->e820_entries++;
		zero_page->e820_map[1].addr =  0x100000UL; // A20 range and up to 4GB
		zero_page->e820_map[1].size =  (pVM->mem.totmegs-1) * 1024 * 1024;
		zero_page->e820_map[1].type = E820_RAM;
	}

	return 0;
}

#define CR0_PE (1<<0)

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

static int vm_loader_prime_linux32(vm_t *pVM) {
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	LOG("Priming for Linux32 (VCPU 0)",0);

	memset(&sregs,0,sizeof(struct kvm_sregs));
	memset(&regs,0,sizeof(struct kvm_regs));

	LOG(" * Copying %lu bytes to 0x%lx",pVM->pLoader->section[0].size,pVM->pLoader->section[0].paddr);
	memcpy( (uint8_t*)pVM->pLowRAM + pVM->pLoader->section[0].paddr,
			pVM->pLoader->section[0].pData,
			pVM->pLoader->section[0].size );

	if (ioctl(pVM->pCPU[0].fd, KVM_GET_SREGS, &sregs) < 0) {
		LOGE("KVM_GET_SREGS");
		exit(1);
	}

//	{
		struct kvm_segment seg;
		uint64_t *gdt;

		seg.base = 0;
		seg.limit = 0xffffffff;
		seg.present = 1;
		seg.dpl = 0;
		seg.db = 1;
		seg.s = 1; /* Code/data */
		seg.l = 0;
		seg.g = 1; /* 4KB granularity */

		sregs.cr0 |= CR0_PE; /* enter protected mode */
		sregs.gdt.base = 0x1000;
		sregs.gdt.limit = 4 * 8 - 1;

		gdt = (uint64_t *)((uint8_t*)pVM->pLowRAM + sregs.gdt.base);
		/* gdt[0] is the null segment */

		seg.type = 11; /* Code: execute, read, accessed */
		seg.selector = 0x10;
		fill_segment_descriptor(gdt, &seg);
		sregs.cs = seg;

		seg.type = 3; /* Data: read/write, accessed */
		seg.selector = 0x18;
		fill_segment_descriptor(gdt, &seg);
		sregs.ds = sregs.es = sregs.fs = sregs.gs = sregs.ss = seg;
//	}

	if (ioctl(pVM->pCPU[0].fd, KVM_SET_SREGS, &sregs) < 0) {
		LOGE("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = pVM->pLoader->entrypoint;

	LOG(" * Linux32 entrypoint=0x%lx",regs.rip);

	regs.rbp = regs.rdi = regs.rbx = 0;
	regs.rsi = 0x7E00; // Points to the boot_params table

	if( ioctl(pVM->pCPU[0].fd, KVM_SET_REGS, &regs) < 0 ) {
		LOGE("KVM_SET_REGS");
		exit(1);
	}

	vm_loader_create_bootparams(pVM);

	return 0;
}

/* Builds a GDT at 0x1000 (guest) */
static void build_gdt(vm_t *pVM) {
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

	gdt = (uint64_t *)((uint8_t*)pVM->pLowRAM + 0x1000);
	/* gdt[0] is the null segment */

	seg.type = 11; /* Code: execute, read, accessed */
	seg.selector = 0x10;
	fill_segment_descriptor(gdt, &seg);

	seg.type = 3; /* Data: read/write, accessed */
	seg.selector = 0x18;
	fill_segment_descriptor(gdt, &seg);
}

static void flat_reg(struct kvm_segment *pSeg) {
	pSeg->base = 0;
	pSeg->limit = 0xffffffff;
	pSeg->present = 1;
	pSeg->dpl = 0;
	pSeg->db = 1;
	pSeg->s = 1; /* Code/data */
	pSeg->l = 0;
	pSeg->g = 1; /* 4KB granularity */
}

static int vm_loader_prime_linux64(vm_t *pVM) {
	struct kvm_sregs sregs;
	struct kvm_regs regs;
	int i;

	LOG("Priming for Linux64");

	LOG(" * Copying %lu bytes to 0x%lx (Kernel)",pVM->pLoader->section[0].size,pVM->pLoader->section[0].paddr);
	memcpy( (uint8_t*)pVM->pLowRAM + pVM->pLoader->section[0].paddr,
			pVM->pLoader->section[0].pData,
			pVM->pLoader->section[0].size );

	build_gdt(pVM);

	for(i=0;i<pVM->numcpus;i++) {
		LOG(" * Setting up VCPU%i",i);

		memset(&sregs,0,sizeof(struct kvm_sregs));
		memset(&regs,0,sizeof(struct kvm_regs));

		// Setup segment descriptors
		if (ioctl(pVM->pCPU[i].fd, KVM_GET_SREGS, &sregs) < 0) {
			LOGE("KVM_GET_SREGS");
			exit(1);
		}

		sregs.cr0 |= CR0_PE; /* enter protected mode */
		sregs.gdt.base = 0x1000;
		sregs.gdt.limit = 4 * 8 - 1;

		flat_reg(&sregs.cs);
		sregs.cs.type = 11; /* Code: execute, read, accessed */
		sregs.cs.selector = 0x10;

		flat_reg(&sregs.ds);
		sregs.ds.type = 3; /* Data: read/write, accessed */
		sregs.ds.selector = 0x18;
		sregs.es = sregs.fs = sregs.gs = sregs.ss = sregs.ds;

		if( ioctl(pVM->pCPU[i].fd, KVM_SET_SREGS, &sregs) < 0 ) {
			LOGE("KVM_SET_SREGS");
			exit(1);
		}

		// Setup regular registers
		memset(&regs, 0, sizeof(regs));
		/* Clear all FLAGS bits, except bit 1 which is always set. */
		regs.rflags = 2;
		regs.rip = pVM->pLoader->entrypoint;

		LOG("  * RIP=0x%lx",regs.rip);

		regs.rbp = regs.rdi = regs.rbx = 0;
		regs.rsi = 0x7E00; // Points to the boot_params table

		if (ioctl(pVM->pCPU[i].fd, KVM_SET_REGS, &regs) < 0) {
			LOGE("KVM_SET_REGS");
			exit(1);
		}
	}

	vm_loader_create_bootparams(pVM);

	return 0;
}


int  vm_loader_primeVM(struct vm *pVM) {

	if(      pVM->pLoader->type == eImagetype_Linux32 ) return vm_loader_prime_linux32(pVM);
	else if( pVM->pLoader->type == eImagetype_Linux64 ) return vm_loader_prime_linux64(pVM);
	else {
		LOGE("Unknown image-type 0x%x",pVM->pLoader->type);
	}

	ASSERT(0,"Failed to prime\n");

	return -1;	
}

int  vm_loader_loadFile(struct vm *pVM,const char *pzFile) {

	if( pVM == NULL ) return -1;

	pVM->pLoader = (vmloader_t*)calloc(1,sizeof(vmloader_t));

	memset(pVM->pLoader->section,0,sizeof(pVM->pLoader->section));
#if 0
	// To free..
	for(int i=0;i<4;i++) {
		if( section[i].pFreeable != NULL ) free(section[i].pFreeable);
	}
#endif

	if( vm_loader_load_bzImage(pVM->pLoader,pzFile) != 0 ) {
		LOGE("Failed to open image \"%s\"",pzFile);
		return -1;
	}
#if 0
	if( vm_loader_primeVM(pVM) != 0 ) {
		LOGE("Failed to prime VM");
		return -2;
	}
#endif
//	printf("Failed to load\n");

	return 0;
}
#endif
