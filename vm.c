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

#include "kvmhelper.h"
#include "vm.h"
#include "vm_internal.h"
#include "hw/io.h"
#include "hw/devices.h"

static int setup_vm(vm_t *pVM) {
	int r = 0;

	pVM->fd_kvm = open("/dev/kvm", O_RDWR);
	if( pVM->fd_kvm < 0) {
		LOGE("Failed to open KVM device");
		r = -1;
		goto error_exit;
	}

 	r = ioctl(pVM->fd_kvm, KVM_GET_API_VERSION, 0);
	if( r != KVM_API_VERSION) {
		LOG("KVM version mismatch");
		r = -2;
		goto error_exit;
	}

	pVM->fd_vm = ioctl(pVM->fd_kvm, KVM_CREATE_VM, NULL);
	if( pVM->fd_vm < 0 ) {
		LOG("Failed to create KVM VM");
		r = -3;
		goto error_exit;
	}

	r = ioctl(pVM->fd_vm, KVM_SET_TSS_ADDR, 0xfffbd000);
	if( r < 0 ) {
		LOG("Failed KVM_SET_TSS_ADDR ioctl");
		r = -4;
		goto error_exit;
	}

	struct kvm_pit_config pit_config = { .flags = 0, };
	r = ioctl(pVM->fd_vm, KVM_CREATE_PIT2, &pit_config);
	if( r < 0 ) {
		LOG("Failed KVM_CREATE_PIT2 ioctl");
		r = -5;
		goto error_exit;
	}
#if 0
	r = ioctl(pVM->fd_vm, KVM_CREATE_IRQCHIP);
	if( r < 0 ) {
		LOG("Failed KVM_CREATE_IRQCHIP ioctl");
		r = -6;
		goto error_exit;
	}
#endif
	return 0;
error_exit:
	if(  pVM->fd_kvm != -1 ) close(pVM->fd_kvm);
	return r;
}

static int map_ram(vm_t *pVM,uint64_t phys,void *pUserspaceAddr,uint64_t len) {
	struct kvm_userspace_memory_region mem;
	int r;

	mem.slot            = ++pVM->memslot;
	mem.guest_phys_addr = phys;
	mem.memory_size     = len;
	mem.userspace_addr  = (unsigned long long)pUserspaceAddr;
	mem.flags           = 0;

	r = ioctl(pVM->fd_vm, KVM_SET_USER_MEMORY_REGION, &mem);
	if( r < 0 ) {
		LOG("Failed to map memory");
		return r;
	}
	return 0;
}

static int setup_ram(vm_t *pVM,uint16_t megram) {
	void *userspace_addr;

	userspace_addr = mmap(0, megram*1024*1024, PROT_EXEC | PROT_READ | PROT_WRITE,
	      MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	pVM->pLowRAM = userspace_addr;
	pVM->mem.totmegs = megram;

	LOG("LowRAM = %p",pVM->pLowRAM);

	map_ram(pVM,0x0,userspace_addr,megram*1024*1024);
	map_ram(pVM,0xfffe0000,userspace_addr,0x20000); // Dummy mapping until we fix the reset vector

	return 0;
}

static int setup_cpus(vm_t *pVM,int numcpus) {
	if( pVM == NULL ) return -1;

	pVM->numcpus = numcpus;
	pVM->pCPU = (vcpu_t*)calloc(numcpus,sizeof(vcpu_t));


	for(int iCPU=0;iCPU<numcpus;iCPU++) {
		pVM->pCPU[iCPU].fd = ioctl(pVM->fd_vm, KVM_CREATE_VCPU, iCPU);
		if( pVM->pCPU[iCPU].fd < 0 ) {
			LOG("Failed KVM_CREATE_VCPU ioctl");
			return -1;
		}

		pVM->pCPU[iCPU].stat_len = ioctl(pVM->fd_kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
		if( pVM->pCPU[iCPU].stat_len < 0 ) {
			LOG("Failed KVM_GET_VCPU_MMAP_SIZE ioctl");
			return -2;
		}

		// mmap the per-VCPU state structure onto our process space
		pVM->pCPU[iCPU].stat = mmap(0, pVM->pCPU[iCPU].stat_len, PROT_READ | PROT_WRITE, MAP_SHARED,pVM->pCPU[iCPU].fd, 0);
		if( pVM->pCPU[iCPU].stat == (void*)-1 ) {
			LOG("Failed to MMAP per-cpu-state struct");
			pVM->pCPU[iCPU].stat = NULL;
			return -3;
		}

		vm_cpuutil_setup_cpuid(pVM,&pVM->pCPU[iCPU]);
		vm_cpuutil_setup_fpu(&pVM->pCPU[iCPU]);

		LOG("Created VCPU %i (fd=%i)",iCPU,pVM->pCPU[iCPU].fd);
	}

	return 0;
}

struct vm *vm_create(int numcpus,uint16_t megram) {
	vm_t *pVM = NULL;

	pVM = (vm_t*)calloc(1,sizeof(vm_t));

	if( setup_vm(pVM) != 0 ) goto err_exit;
	if( setup_ram(pVM,megram) != 0 ) goto err_exit;

	int r = ioctl(pVM->fd_vm, KVM_CREATE_IRQCHIP);
	if( r < 0 ) {
		LOG("Failed KVM_CREATE_IRQCHIP ioctl");
		r = -6;
		goto err_exit;
	}

	pVM->numcpus = numcpus;

	vm_cpuutil_setup_mptables(pVM);

	if( setup_cpus(pVM,numcpus) != 0 ) goto err_exit;

	pVM->state = eVMState_Created;

	LOGI("VM created");

	return pVM;
err_exit:
	if( pVM != NULL ) free(pVM);
	pVM->state = eVMState_FailedCreate;
	return NULL;
}

void       vm_destroy(struct vm *pVM) {
	if( pVM == NULL ) return;

	if( pVM->pCPU != NULL ) {
		free(pVM->pCPU);
	}
	memset(pVM,0,sizeof(vm_t));

	free(pVM);

	LOGI("VM destroyed");
}

int        vm_loadkernel(struct vm *pVM,const char *pzKernelfile) {

	if( pVM == NULL ) return -1;
	if( pVM->state != eVMState_Created ) return -2;

	vm_loader_loadFile(pVM,pzKernelfile);
	vm_loader_primeVM(pVM);

	return 0;
}

static inline int handle_io(struct kvm_run *pStat) {
	unsigned int c;

	if( pStat->io.direction == KVM_EXIT_IO_OUT ) {
		//LOG("IO_OUT Port=0x%x, Size=%i",pStat->io.port,pStat->io.size);
		for(c=0;c<pStat->io.count;c++) {
			if(        pStat->io.size == 1 ) {
				io_outb(pStat->io.port,*(uint8_t*)((uint8_t*)pStat + pStat->io.data_offset) );
			} else if( pStat->io.size == 2 ) {
				io_outw(pStat->io.port,*(uint16_t*)((uint8_t*)pStat + pStat->io.data_offset) );
			} else if( pStat->io.size == 4 ) {
				io_outl(pStat->io.port,*(uint32_t*)((uint8_t*)pStat + pStat->io.data_offset) );
			} else {
				ASSERT(0,"Unknown out-size %i",pStat->io.size);
			}
			pStat->io.data_offset += pStat->io.size;
		}
		return 0;
	} else { // KVM_EXIT_IO_IN
		//LOG("IO_IN Port=0x%x, Size=%i",pStat->io.port,pStat->io.size);
		for(c=0;c<pStat->io.count;c++) {
			if(        pStat->io.size == 1 ) {
				*(uint8*)( (uint8_t*)pStat + pStat->io.data_offset ) = io_inb(pStat->io.port);
			} else if( pStat->io.size == 2 ) {
				*(uint16*)( (uint8_t*)pStat + pStat->io.data_offset ) = io_inw(pStat->io.port);
			} else if( pStat->io.size == 4 ) {
				*(uint32*)( (uint8_t*)pStat + pStat->io.data_offset ) = io_inl(pStat->io.port);
			} else {
				ASSERT(0,"Unknown in-size %i",pStat->io.size);
			}
			pStat->io.data_offset += pStat->io.size;
		}
		return 0;
	}
}


static void *cpu_thread(void *pArg) {
	vcpu_t *pCPU = (vcpu_t*)pArg;
	vm_t *pVM = NULL;
    struct kvm_run  *pStat;
	int r,bDone = 0;

	if( pCPU == NULL ) return NULL;

	pVM = pCPU->thread.pVM;
	(void)pVM;

	LOG("Starting thread for VCPU %i (fd=%i)",pCPU->thread.vcpu,pCPU->fd);

	while(!bDone) {
		r = ioctl(pCPU->fd, KVM_RUN, 0);
		//LOG("r=%i",r);
		(void)r;

		pStat = (struct kvm_run*)pCPU->stat;

		if( pStat->exit_reason == KVM_EXIT_IO ) {
			if( handle_io(pStat) != 0 ) {
			}
		} else if( pStat->exit_reason == KVM_EXIT_UNKNOWN ) {
	    } else {
			//LOGE("Unhandled KVM Exit-reason: %u (%s)",pStat->exit_reason,kvm_exitreason_str[pStat->exit_reason]);
			//pMe->pVM->dump_regs();
			if( pStat->exit_reason == KVM_EXIT_FAIL_ENTRY ) {
				LOG("HW Entry failure reason: %016llx",pStat->fail_entry.hardware_entry_failure_reason);
			}
			vm_cpuutil_dump_regs(pCPU);
			ASSERT(0,"Unhandled KVM Exit-reason: %u (%s)",pStat->exit_reason,kvm_exitreason_str[pStat->exit_reason]);
	    }
	}

	return NULL;
}

int        vm_run(struct vm *pVM) {
	if( pVM == NULL ) return -1;

	devices_init();

	for(int iCPU=0;iCPU<pVM->numcpus;iCPU++) {

		pVM->pCPU[iCPU].thread.vcpu = iCPU;
		pVM->pCPU[iCPU].thread.pVM  = pVM;

		pthread_create(&pVM->pCPU[iCPU].thread.hdl,NULL,cpu_thread,&pVM->pCPU[iCPU]);
	}

	pVM->state = eVMState_Running;

	return 0;	
}

int        vm_pause(struct vm *pVM) {
	if( pVM == NULL ) return -1;

	return 0;
}

eVMState   vm_getstate(struct vm *pVM) {
	if( pVM == NULL ) return eVMState_Invalid;

	return pVM->state;
}
