#pragma once

#include <stdint.h>
#include <pthread.h>
#include <linux/kvm.h>

typedef struct vm {
	int fd_kvm; // FD to /dev/kvm (typically)
	int fd_vm;  // FD to the create VM

	struct vcpu *pVCPU; // Array of config.numcpus per-cpu structs

	struct {
		int   currslot;
		void *pLow;  // 0G->3G
		void *pHigh; // 4G->
	} ram;

	struct {
		int numsections;
		struct kernelsection *section;

		uint64_t entrypoint;
	} kernel;

	struct {
		int numcpus;
		int ramsize; // In MB
	} config;

	struct {
		int out,in;
	} console;

	struct {
		uint32_t numexits;
	} stats;
} vm_t;

typedef struct vcpu {
	int fd; // FD to created virtual CPU

	void  *state;
	size_t state_len;

	struct kvm_fpu fpu;
	struct kvm_msrs *pMSRS;

	// Thread / IPC stuff
	struct {
		int vcpu;

		struct vcputhread *priv;
		pthread_t hdl;
		struct vm *pVM;
	} thread;

//	struct vm *pVM; // Pointer to associated VM
} vcpu_t;

int intvm_setup(vm_t *pVM);
int intvm_memory_setup(vm_t *pVM);
int intvm_biostables_setup(vm_t *pVM);
int intvm_cpus_setup(vm_t *pVM);

int intvm_memory_release(vm_t *pVM);
int intvm_cpus_release(vm_t *pVM);

int intvm_loader_loadkernel(vm_t *pVM,const char *pzFilename);
int intvm_loader_release(vm_t *pVM);

int intvm_cpus_start(vm_t *pVM);

void *intvm_memory_getguestspaceptr(vm_t *pVM,uint64_t guestaddr);

int intvm_irq_set(int irq,int level);
int intvm_get_console(vm_t *pVM,int *out,int *in);
