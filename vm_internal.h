#pragma once

#include "vm.h"

typedef struct {
	// KVM stuff
	int fd;
	void *stat;
	size_t stat_len;

	struct kvm_fpu fpu;

	// Thread / IPC stuff
	struct {
		int vcpu;
		pthread_t hdl;
		struct vm *pVM;
	} thread;
} vcpu_t;

typedef struct vm {
	int fd_vm,fd_kvm;

	eVMState state;

	// Memory
	struct {
		uint16_t totmegs;
	} mem;
	int memslot;
	void *pLowRAM;  // 0G->3G
	void *pHighRAM; // 4G->

	// CPU
	int numcpus;
	vcpu_t *pCPU;

	// Loader
	struct vmloader *pLoader;
} vm_t;

int  vm_loader_loadFile(struct vm *pVM,const char *pzFile);
int  vm_loader_primeVM(struct vm *pVM);
int  vm_cpuutil_setup_cpuid(vm_t *pVM,vcpu_t *pVCPU);
int  vm_cpuutil_setup_fpu(vcpu_t *pCPU);
int  vm_cpuutil_setup_mptables(vm_t *pVM);
void vm_cpuutil_dump_regs(vcpu_t *pCPU);

