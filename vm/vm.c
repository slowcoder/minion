#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>
#include "caos/log.h"

#include "vm/vm_internal.h"

int intvm_setup(vm_t *pVM) {
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

	struct kvm_pit_config pit_config = { .flags = 1, };
	r = ioctl(pVM->fd_vm, KVM_CREATE_PIT2, &pit_config);
	if( r < 0 ) {
		LOG("Failed KVM_CREATE_PIT2 ioctl");
		r = -5;
		goto error_exit;
	}

	r = ioctl(pVM->fd_vm, KVM_CREATE_IRQCHIP);
	if( r < 0 ) {
		LOG("Failed KVM_CREATE_IRQCHIP ioctl");
		r = -6;
		goto error_exit;
	}

	return 0;
error_exit:
	if(  pVM->fd_kvm != -1 ) close(pVM->fd_kvm);
	return r;
}
