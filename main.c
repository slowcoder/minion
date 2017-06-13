#include <stdio.h>
#include <unistd.h>

#include "caos/log.h"
#include "vm/vm_api.h"

#define KERNEL_FILE "kernels/bzImage.64"
//#define KERNEL_FILE "kernels/bzImage-3.16.44.64"

int main(void) {
	struct vm *pVM;
	int r = 0;

	pVM = vm_create(1,1024);
	if( pVM == NULL ) {
		LOGE("Failed to create VM");
		r = -1;
		goto err_exit;
	}

	if( vm_loadkernel(pVM,KERNEL_FILE) != 0 ) {
		LOGE("Failed to load kernel");
		r = -2;
		goto err_exit;
	}

	vm_run(pVM);

	LOG("VM running. Sleeping main thread..");
	sleep(5);
	vm_destroy(pVM);
	LOG("Done. Bye!");
	return 0;

err_exit:
	vm_destroy(pVM);

	return r;
}