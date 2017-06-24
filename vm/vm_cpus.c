#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>
#include <linux/kvm.h>
#include <errno.h>
#include "hw/devices.h"

#include "caos/log.h"

#include "kvmhelper.h"
#include "vm/vm_internal.h"

int  intvm_setup_cpuid(vm_t *pVM,vcpu_t *pVCPU);
int  intvm_setup_fpu(vcpu_t *pCPU);
int  intvm_setup_lapic(vcpu_t *pVCPU);
int  intvm_setup_msrs(vcpu_t *pVCPU);

static __thread vcpu_t *current_vcpu;

typedef struct vcputhread {
	volatile int bRunning,bShouldStop;
} vcputhread_t;

static void handle_sigusr1(int sig) {
	LOG("CPU%i got SIGUSR1",current_vcpu->thread.vcpu);

	if(current_vcpu->thread.priv->bShouldStop) {
		current_vcpu->thread.priv->bRunning = 0;
	}	
}

static inline int handle_io(vcpu_t *pVCPU,struct kvm_run *pState) {
	unsigned int c;

	if( pState->io.direction == KVM_EXIT_IO_OUT ) {
		//LOG("IO_OUT Port=0x%x, Size=%i",pState->io.port,pState->io.size);
		for(c=0;c<pState->io.count;c++) {
			devices_io_out(pState->io.port,pState->io.size,(uint8_t*)pState + pState->io.data_offset);
			pState->io.data_offset += pState->io.size;
		}
	} else if( pState->io.direction == KVM_EXIT_IO_IN ) {
		for(c=0;c<pState->io.count;c++) {
			devices_io_in(pState->io.port,pState->io.size,(uint8_t*)pState + pState->io.data_offset);
			pState->io.data_offset += pState->io.size;
		}
	}
	return 0;
}

static void *cpu_thread(void *pArg) {
	vcpu_t *pVCPU = (vcpu_t*)pArg;
	vm_t *pVM;
    struct kvm_run  *pState;
	int r;
	vcputhread_t *pThread = NULL;

	if( pVCPU == NULL ) return NULL;

	pThread = pVCPU->thread.priv;
	pThread->bRunning = 1;

	pVM = pVCPU->thread.pVM;

	current_vcpu = pVCPU;
	signal(SIGUSR1,handle_sigusr1);

	LOG("Starting thread for VCPU %i (fd=%i)",pVCPU->thread.vcpu,pVCPU->fd);

	while(pThread->bRunning) {
		r = ioctl(pVCPU->fd, KVM_RUN, 0);
		if( (r == -1) && (errno == EINTR) ) {
			LOG("INTR!");
		}

		pVM->stats.numexits++;

		pState = (struct kvm_run*)pVCPU->state;

		if( pState->exit_reason == KVM_EXIT_IO ) {
			handle_io(pVCPU,pState);
//			LOG("IO");
		} else if( pState->exit_reason == KVM_EXIT_MMIO ) {
			LOGD("EXIT_MMIO");
			LOGD(" phys_addr=0x%llx",pState->mmio.phys_addr);
			LOGD(" len=%u",pState->mmio.len);
			LOGD(" is_write=%u",pState->mmio.is_write);

			if( pState->mmio.is_write ) {
				devices_mmio_out(pState->mmio.phys_addr,pState->mmio.len,pState->mmio.data);
			} else {
				devices_mmio_in(pState->mmio.phys_addr,pState->mmio.len,pState->mmio.data);
			}
			//ASSERT(0,"EXIT_MMIO not implemented");
		} else if( pState->exit_reason == KVM_EXIT_INTR ) {
		} else {
			LOGE("Unhandled KVM Exit-reason: %u (%s)",pState->exit_reason,kvm_exitreason_str[pState->exit_reason]);
			pThread->bRunning = 0;
		}
	}
	return NULL;
}


int intvm_cpus_start(vm_t *pVM) {
	if( pVM == NULL ) return -1;

	for(int iCPU=0;iCPU<pVM->config.numcpus;iCPU++) {

		pVM->pVCPU[iCPU].thread.vcpu = iCPU;
		pVM->pVCPU[iCPU].thread.pVM  = pVM;

		pthread_create(&pVM->pVCPU[iCPU].thread.hdl,NULL,cpu_thread,&pVM->pVCPU[iCPU]);
	}

	return 0;
}

int intvm_cpus_setup(vm_t *pVM) {
	if( pVM == NULL ) return -1;

	pVM->pVCPU = (vcpu_t*)calloc(pVM->config.numcpus,sizeof(vcpu_t));

	for(int iCPU=0;iCPU<pVM->config.numcpus;iCPU++) {

		pVM->pVCPU[iCPU].thread.priv = (vcputhread_t*)calloc(1,sizeof(vcputhread_t));

		pVM->pVCPU[iCPU].fd = ioctl(pVM->fd_vm, KVM_CREATE_VCPU, iCPU);
		if( pVM->pVCPU[iCPU].fd < 0 ) {
			LOG("Failed KVM_CREATE_VCPU ioctl");
			return -1;
		}

		pVM->pVCPU[iCPU].state_len = ioctl(pVM->fd_kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
		if( pVM->pVCPU[iCPU].state_len < 0 ) {
			LOG("Failed KVM_GET_VCPU_MMAP_SIZE ioctl");
			return -2;
		}

		// mmap the per-VCPU state structure onto our process space
		pVM->pVCPU[iCPU].state = mmap(0, pVM->pVCPU[iCPU].state_len,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,pVM->pVCPU[iCPU].fd, 0);
		if( pVM->pVCPU[iCPU].state == (void*)-1 ) {
			LOG("Failed to MMAP per-cpu-state struct");
			pVM->pVCPU[iCPU].state = NULL;
			return -3;
		}
		LOGD("VCPU%i state=%p",iCPU,pVM->pVCPU[iCPU].state);

		intvm_setup_cpuid(pVM,&pVM->pVCPU[iCPU]);
		intvm_setup_fpu(&pVM->pVCPU[iCPU]);
		intvm_setup_lapic(&pVM->pVCPU[iCPU]);
		intvm_setup_msrs(&pVM->pVCPU[iCPU]);

		LOG("Created VCPU %i (fd=%i)",iCPU,pVM->pVCPU[iCPU].fd);
	}

	return 0;
}

int intvm_cpus_release(vm_t *pVM) {
	int i;

	if( pVM == NULL ) return -1;

	// Stop and join all VCPU threads
	for(i=0;i<pVM->config.numcpus;i++) {
		pVM->pVCPU[i].thread.priv->bShouldStop = 1;
		pthread_kill(pVM->pVCPU[i].thread.hdl,SIGUSR1);
		pthread_join(pVM->pVCPU[i].thread.hdl,NULL);
	}
	LOG("All VCPUs joined");

	// Release RAM
	if( pVM->pVCPU != NULL ) {
		for(int iCPU=0;iCPU<pVM->config.numcpus;iCPU++) {

			if( pVM->pVCPU[iCPU].pMSRS != NULL )
				free(pVM->pVCPU[iCPU].pMSRS);

			munmap(pVM->pVCPU[iCPU].state,pVM->pVCPU[iCPU].state_len);

			if( pVM->pVCPU[iCPU].thread.priv != NULL ) {
				free(pVM->pVCPU[iCPU].thread.priv);
				pVM->pVCPU[iCPU].thread.priv = NULL;
			}
		}
		free(pVM->pVCPU);
		pVM->pVCPU = NULL;
	}

	return 0;
}

void cpus_test(vm_t *pVM) {

	for(int iCPU=0;iCPU<pVM->config.numcpus;iCPU++) {
		pthread_kill(pVM->pVCPU[iCPU].thread.hdl,SIGUSR1);
	}
}

int intvm_irq_set(int irq,int level) {
	struct kvm_irq_level irq_level;

	irq_level	= (struct kvm_irq_level) {
		{
			.irq		= irq,
		},
		.level		= level,
	};

	vm_t *pVM = current_vcpu->thread.pVM;

	LOGD("IRQ%i=%i",irq,level);

	if (ioctl(pVM->fd_vm, KVM_IRQ_LINE, &irq_level) < 0) {
		ASSERT(0,"KVM_IRQ_LINE failed");
	}

	return 0;
}