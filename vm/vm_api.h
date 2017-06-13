#pragma once

#include <stdint.h>

typedef enum {
	eVMState_Invalid = 0,
	eVMState_FailedCreate,
	eVMState_Created,
	eVMState_Paused,
	eVMState_Running,
	eVMState_Finished_Reset,
	eVMState_Finished_Poweroff
} eVMState;

struct vm *vm_create(int numcpus,uint16_t megram);
void       vm_destroy(struct vm *pVM);
int        vm_loadkernel(struct vm *pVM,const char *pzKernelfile);
int        vm_run(struct vm *pVM);
int        vm_pause(struct vm *pVM);

eVMState   vm_getstate(struct vm *pVM);
