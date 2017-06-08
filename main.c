#include <stdio.h>
#include <unistd.h>

#include "caos/log.h"
#include "vm.h"

int main(void) {
	struct vm *pVM;

	pVM = vm_create(4,1024);
	if( pVM == NULL ) {
		LOGE("Failed to create VM");
	}

	vm_loadkernel(pVM,"kernels/bzImage.64");

	vm_run(pVM);
	sleep(5);

	vm_destroy(pVM);

	return 0;
}