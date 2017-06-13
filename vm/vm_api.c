#include <stdlib.h>
#include "caos/log.h"

#include "vm/vm_api.h"
#include "vm/vm_internal.h"
#include "hw/devices.h"

struct vm *vm_create(int numcpus,uint16_t megram) {
	vm_t *pVM = NULL;

	pVM = (vm_t*)calloc(1,sizeof(vm_t));

	pVM->config.numcpus = numcpus;
	pVM->config.ramsize = megram;

	if( intvm_setup(pVM) != 0 ) {
		LOGE("Failed to setup VM");
		goto err_exit;
	}
	if( intvm_memory_setup(pVM) != 0 ) {
		LOGE("Failed to setup RAM");
		goto err_exit;
	}
	if( intvm_biostables_setup(pVM) != 0 ) {
		LOGE("Failed to setup BIOS-tables");
		goto err_exit;
	}
	if( intvm_cpus_setup(pVM) != 0 ) {
		LOGE("Failed to setup CPUs");
		goto err_exit;
	}

	devices_init();

	return pVM;

err_exit:
	if( pVM != NULL ) {
		vm_destroy(pVM);
	}
	return NULL;
}

void       vm_destroy(struct vm *pVM) {
	if( pVM == NULL ) return;

	intvm_cpus_release(pVM);
	intvm_memory_release(pVM);

	free(pVM);
}

int        vm_loadkernel(struct vm *pVM,const char *pzKernelfile) {
	int r;

	if( pVM == NULL ) return -1;
	if( pzKernelfile == NULL ) {
		LOGE("NULL filename for kernel?");
		return -2;
	}

	r = intvm_loader_loadkernel(pVM,pzKernelfile);

	return r;
}

int        vm_run(struct vm *pVM) {

	if( intvm_cpus_start(pVM) != 0 ) {
		LOGE("Failed to start VCPUs");
		return -1;
	}

	return 0;
}

int        vm_pause(struct vm *pVM) {
	ASSERT(0,"TODO: Implement VM pausing");
	return -1;
}

eVMState   vm_getstate(struct vm *pVM) {
	return eVMState_Invalid;
}
