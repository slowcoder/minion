#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>
#include "caos/log.h"

#include "vm/vm_internal.h"

#define	MAX_KVM_CPUID_ENTRIES		100

#include <pci/types.h> // To get u32.. Need better way..
#include "cpufeature.h"
static void fixup_cpuid(struct kvm_cpuid2 *pKVMCPU) {
	unsigned int i;
	struct cpuid_regs regs;

	for(i=0;i<pKVMCPU->nent;i++) {
		struct kvm_cpuid_entry2 *pEntry = &pKVMCPU->entries[i];

		switch(pEntry->function) {
			case 0:
				regs = (struct cpuid_regs) {
					.eax = 0x00,
				};
		        host_cpuid(&regs);
				/* Vendor name */
				pEntry->ebx = regs.ebx;
				pEntry->ecx = regs.ecx;
				pEntry->edx = regs.edx;
				break;
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

int  intvm_setup_cpuid(vm_t *pVM,vcpu_t *pVCPU) {
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
#include "apicdef.h"
int intvm_setup_lapic(vcpu_t *pVCPU) {
	struct local_apic lapic;

	if (ioctl(pVCPU->fd, KVM_GET_LAPIC, &lapic))
		return -1;

	lapic.lvt_lint0.delivery_mode = APIC_MODE_EXTINT;
	lapic.lvt_lint1.delivery_mode = APIC_MODE_NMI;

	return ioctl(pVCPU->fd, KVM_SET_LAPIC, &lapic);
}

#include <asm/msr-index.h>
static struct kvm_msrs *kvm_msrs__new(size_t nmsrs)
{
	struct kvm_msrs *vcpu = calloc(1, sizeof(*vcpu) + (sizeof(struct kvm_msr_entry) * nmsrs));

	if (!vcpu)
		return NULL;

	return vcpu;
}

#define KVM_MSR_ENTRY(_index, _data)	\
	(struct kvm_msr_entry) { .index = _index, .data = _data }

int intvm_setup_msrs(vcpu_t *pVCPU) {
	unsigned long ndx = 0;

	pVCPU->pMSRS = kvm_msrs__new(100);
	if( pVCPU->pMSRS == NULL ) return -1;

	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_IA32_SYSENTER_CS,	0x0);
	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_IA32_SYSENTER_ESP,	0x0);
	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_IA32_SYSENTER_EIP,	0x0);
#if 1 // This is for x86_64 hosts+guests only
	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_STAR,			0x0);
	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_CSTAR,			0x0);
	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_KERNEL_GS_BASE,		0x0);
	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_SYSCALL_MASK,		0x0);
	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_LSTAR,			0x0);
#endif
	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_IA32_TSC,		0x0);
	pVCPU->pMSRS->entries[ndx++] = KVM_MSR_ENTRY(MSR_IA32_MISC_ENABLE,
						MSR_IA32_MISC_ENABLE_FAST_STRING);

	pVCPU->pMSRS->nmsrs = ndx;

	return ioctl(pVCPU->fd, KVM_SET_MSRS, pVCPU->pMSRS);
}

int intvm_setup_fpu(vcpu_t *pVCPU) {
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

#define CR0_PE (1<<0)

int intvm_setup_gpregs(vm_t *pVM,uint64_t rip,uint64_t rsi) {
	struct kvm_sregs sregs;
	struct kvm_regs regs;
	int i;

	if( pVM == NULL ) return -1;

	for(i=0;i<pVM->config.numcpus;i++) {
		LOGD(" * Setting up VCPU%i",i);

		memset(&sregs,0,sizeof(struct kvm_sregs));
		memset(&regs,0,sizeof(struct kvm_regs));

		// Setup segment descriptors
		if (ioctl(pVM->pVCPU[i].fd, KVM_GET_SREGS, &sregs) < 0) {
			LOGE("KVM_GET_SREGS");
			return -2;
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

		if( ioctl(pVM->pVCPU[i].fd, KVM_SET_SREGS, &sregs) < 0 ) {
			LOGE("KVM_SET_SREGS");
			return -3;
		}

		// Setup regular registers
		memset(&regs, 0, sizeof(regs));
		/* Clear all FLAGS bits, except bit 1 which is always set. */
		regs.rflags = 2;
		regs.rip = rip;

		LOGD("  * RIP=0x%lx",regs.rip);

		regs.rbp = regs.rdi = regs.rbx = 0;
		regs.rsi = rsi; // Points to the boot_params table

		if (ioctl(pVM->pVCPU[i].fd, KVM_SET_REGS, &regs) < 0) {
			LOGE("KVM_SET_REGS");
			return -4;
		}
	}
	return 0;
}
