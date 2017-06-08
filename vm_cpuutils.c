#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <linux/kvm.h>

#include "caos/log.h"

#include "vm.h"
#include "vm_internal.h"

#define	MAX_KVM_CPUID_ENTRIES		100

static void fixup_cpuid(struct kvm_cpuid2 *pKVMCPU) {
	unsigned int i;

	for(i=0;i<pKVMCPU->nent;i++) {
		struct kvm_cpuid_entry2 *pEntry = &pKVMCPU->entries[i];

		switch(pEntry->function) {
			// TODO: Handle function0 (vendor name) here
			case 1:
				/* Set X86_FEATURE_HYPERVISOR */
				if (pEntry->index == 0) pEntry->ecx |= (1 << 31);
				/* Set CPUID_EXT_TSC_DEADLINE_TIMER*/
				if (pEntry->index == 0) pEntry->ecx |= (1 << 24);
				break;
			case 6:
				/* Clear X86_FEATURE_EPB */
				pEntry->ecx = pEntry->ecx & ~(1 << 3);
				break;
			default:
				break;
		}
	}
}

int  vm_cpuutil_setup_cpuid(vm_t *pVM,vcpu_t *pVCPU) {
	struct kvm_cpuid2 *kvm_cpuid;

	kvm_cpuid = (struct kvm_cpuid2 *)calloc(1, sizeof(*kvm_cpuid) +
				MAX_KVM_CPUID_ENTRIES * sizeof(*kvm_cpuid->entries));

	kvm_cpuid->nent = MAX_KVM_CPUID_ENTRIES;
	if (ioctl(pVM->fd_kvm, KVM_GET_SUPPORTED_CPUID, kvm_cpuid) < 0) {
		LOG("Failed KVM_GET_SUPPORTED_CPUID ioctl");
		return -1;
	}

	// TODO: Filter out capabilities the guest shouldn't have,
	//       and set the ones it should have
	fixup_cpuid(kvm_cpuid);

	if (ioctl(pVCPU->fd, KVM_SET_CPUID2, kvm_cpuid) < 0) {
		LOG("Failed KVM_SET_CPUID2 ioctl");
		return -2;
	}

	free(kvm_cpuid);
	return 0;
}

int vm_cpuutil_setup_fpu(vcpu_t *pVCPU) {
	pVCPU->fpu = (struct kvm_fpu) {
		.fcw	= 0x37f,
		.mxcsr	= 0x1f80,
	};

	if (ioctl(pVCPU->fd, KVM_SET_FPU, &pVCPU->fpu) < 0) {
		LOGE("KVM_SET_FPU failed");
		return -1;
	}
	return 0;
}

#define LOGSEGMENT(__l,__x) { LOG("%s: b=%x, l=%x, s=%x, t=%x, p=%x, dpl=%x",__l,__x.base,__x.limit,__x.selector,__x.type,__x.present,__x.dpl); }

void vm_cpuutil_dump_regs(vcpu_t *pCPU) {
	struct kvm_regs regs;
	struct kvm_sregs sregs;

	if(ioctl(pCPU->fd, KVM_GET_REGS, &regs) < 0)
    	return;
	if(ioctl(pCPU->fd, KVM_GET_SREGS, &sregs) < 0)
		return;

	LOG("--[ VCPU%i REGS ]--",pCPU->thread.vcpu);
	LOG("RIP: %llx:%llx",sregs.cs.base,regs.rip);
	LOG("CS=%04x SS=%04x DS=%04x ES=%04x FS=%04x FS=%04x",
		sregs.cs.selector,
		sregs.ss.selector,
		sregs.ds.selector,
		sregs.es.selector,
		sregs.fs.selector,
		sregs.gs.selector);
	LOG("CR0=%x CR2=%x CR3=%x CR4=%x CR8=%x",sregs.cr0,sregs.cr2,sregs.cr3,sregs.cr4,sregs.cr8);
	LOG("RAX=%x RBX=%x RCX=%x RDX=%x",regs.rax,regs.rbx,regs.rcx,regs.rdx);
	LOG("RSI=%x RDI=%x RSP=%x RBP=%x",regs.rsi,regs.rdi,regs.rsp,regs.rbp);
	LOG("RFLAGS=%x",regs.rflags);
	LOGSEGMENT(" CS",sregs.cs);
	LOGSEGMENT(" DS",sregs.ds);
	LOGSEGMENT(" ES",sregs.es);
	LOGSEGMENT(" FS",sregs.fs);
	LOGSEGMENT(" GS",sregs.gs);
	LOGSEGMENT(" SS",sregs.ss);
	LOGSEGMENT(" TR",sregs.tr);
	LOGSEGMENT("LDT",sregs.tr);
	LOG("GDT: b=%x, l=%x",sregs.gdt.base,sregs.gdt.limit);
	LOG("IDT: b=%x, l=%x",sregs.idt.base,sregs.idt.limit);

	LOG("--[ APIC ]--");
	LOG(" efer: %016llx  apic base: %016llx  nmi: %s",
		(uint64_t) sregs.efer, (uint64_t) sregs.apic_base,
		"Unknown");
//		(vcpu->kvm->nmi_disabled ? "disabled" : "enabled"));

}

typedef struct mp_floating_pointer_structure {
    char signature[4];
    uint32_t configuration_table;
    uint8_t length; // In 16 bytes (e.g. 1 = 16 bytes, 2 = 32 bytes)
    uint8_t mp_specification_revision;
    uint8_t checksum; // This value should make all bytes in the table equal 0 when added together
    uint8_t default_configuration; // If this is not zero then configuration_table should be 
                                   // ignored and a default configuration should be loaded instead
    uint32_t features; // If bit 7 is then the IMCR is present and PIC mode is being used, otherwise 
                       // virtual wire mode is; all other bits are reserved
} mp_fps_t;

typedef struct mp_configuration_table {
    char signature[4]; // "PCMP"
    uint16_t length;
    uint8_t mp_specification_revision;
    uint8_t checksum; // Again, the byte should be all bytes in the table add up to 0
    char oem_id[8];
    char product_id[12];
    uint32_t oem_table;
    uint16_t oem_table_size;
    uint16_t entry_count; // This value represents how many entries are following this table
    uint32_t lapic_address; // This is the memory mapped address of the local APICs 
    uint16_t extended_table_length;
    uint8_t extended_table_checksum;
    uint8_t reserved;
} mp_ct_t;

typedef struct entry_processor {
    uint8_t type; // Always 0
    uint8_t local_apic_id;
    uint8_t local_apic_version;
    uint8_t flags; // If bit 0 is clear then the processor must be ignored
                   // If bit 1 is set then the processor is the bootstrap processor
    uint32_t signature;
    uint32_t feature_flags;
    uint64_t reserved;
} cpuentry_t;

typedef struct entry_io_apic {
    uint8_t type; // Always 2
    uint8_t id;
    uint8_t version;
    uint8_t flags; // If bit 0 is set then the entry should be ignored
    uint32_t address; // The memory mapped address of the IO APIC is memory
} ioapicentry_t;

static unsigned int sumdata(void *p,int len) {
	unsigned int r = 0;
	int i;
	uint8_t *pD = (uint8_t*)p;

	for(i=0;i<len;i++) {
		r += pD[i];
	}
	return r;
}

int  vm_cpuutil_setup_mptables(vm_t *pVM) {
	cpuentry_t *pCPU;
	unsigned int csum;
	uint8_t *pEntry;
	int i;

	mp_fps_t *pFPS = (mp_fps_t*)( pVM->pLowRAM + 0xF0000 );
	memset(pFPS,0,sizeof(mp_fps_t));

	// Signature
	pFPS->signature[0] = '_';
	pFPS->signature[1] = 'M';
	pFPS->signature[2] = 'P';
	pFPS->signature[3] = '_';
	pFPS->length = 1; // 16 bytes
	pFPS->mp_specification_revision = 4;
	pFPS->configuration_table = 0xF0010;

	csum = sumdata(pFPS,sizeof(mp_fps_t));
	csum = 0x100 - (csum & 0xFF);
	pFPS->checksum = csum;

	mp_ct_t *pCT = (mp_ct_t*)( pVM->pLowRAM + 0xF0010 );
	memset(pCT,0,sizeof(mp_ct_t));

	// Signature
	pCT->signature[0] = 'P';
	pCT->signature[1] = 'C';
	pCT->signature[2] = 'M';
	pCT->signature[3] = 'P';
	pCT->length = sizeof(mp_ct_t);
	pCT->mp_specification_revision = 4;
	pCT->lapic_address = 0xFEE00000;
	strcpy(pCT->oem_id,"KVMOEM");
	strcpy(pCT->product_id,"KVMPRODUCT");
	//pCT->entry_count = pVM->numcpus;

	pEntry = pVM->pLowRAM + 0xF0010 + sizeof(mp_ct_t);
	for(i=0;i<pVM->numcpus;i++) {
		pCPU = (cpuentry_t*)pEntry;
		pCPU->type = 0;
		pCPU->local_apic_id = i;
		pCPU->local_apic_version = 0x14; // TODO: Get this from KVM or the host
		pCPU->flags = 1; // Enabled
		if (i == 0) pCPU->flags |= (1<<1); // BP
		pCPU->signature = 0;

		pEntry += 20; // CPU entries are 20 bytes
		pCT->length += 20;
		pCT->entry_count++;
	}

	ioapicentry_t *pIOAPIC;

	for(i=0;i<1;i++) {
		pIOAPIC = (ioapicentry_t*)pEntry;
		pIOAPIC->type = 2; // IO-APIC
		pIOAPIC->id   = i;
		pIOAPIC->version = 0x17;
		pIOAPIC->flags = 1;
		pIOAPIC->address = 0xfec00000;
		pEntry += 8;
		pCT->length += 8;
		pCT->entry_count++;
	}

	csum = sumdata(pCT,pCT->length);
	csum = 0x100 - (csum & 0xFF);
	pCT->checksum = csum;


	return 0;
}