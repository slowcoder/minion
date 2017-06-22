#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>
#define LOGLEVEL LOGLEVEL_DEBUG
#include "caos/log.h"

#include "vm/vm_internal.h"

static int map_ram(vm_t *pVM,uint64_t phys,void *pUserspaceAddr,uint64_t len) {
	struct kvm_userspace_memory_region mem;
	int r;

	mem.slot            = ++pVM->ram.currslot;
	mem.guest_phys_addr = phys;
	mem.memory_size     = len;
	mem.userspace_addr  = (unsigned long long)pUserspaceAddr;
	mem.flags           = 0;

	LOG("Mapping %llx bytes @ 0x%llx",len,phys);

	r = ioctl(pVM->fd_vm, KVM_SET_USER_MEMORY_REGION, &mem);
	if( r < 0 ) {
		LOG("Failed to map memory");
		return r;
	}
	return 0;
}

int intvm_memory_setup(vm_t *pVM) {
	void *userspace_addr;

	//ASSERT(pVM->config.ramsize <= (3*1024),"FIXME: Support more than 3GB of RAM");


	if( pVM->config.ramsize <= (3*1024) ) {
		userspace_addr = mmap(0, (uint64_t)pVM->config.ramsize*1024ULL*1024ULL,
			PROT_EXEC | PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if( userspace_addr == MAP_FAILED ) {
			LOGE("Failed to allocate RAM");
			return -1;
		}

		pVM->ram.pLow = userspace_addr;
		LOGD("LowRAM = %p",pVM->ram.pLow);

		if( map_ram(pVM,0x0,userspace_addr,(uint64_t)pVM->config.ramsize*1024ULL*1024ULL) != 0 ) {
			LOGE("Failed to map low-RAM into VM");
			return -2;
		}
	} else { // >3GB
		userspace_addr = mmap(0, 0xC0000000ULL,
			PROT_EXEC | PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if( userspace_addr == MAP_FAILED ) {
			LOGE("Failed to allocate RAM");
			return -1;
		}

		pVM->ram.pLow = userspace_addr;
		LOGD("LowRAM = %p",pVM->ram.pLow);

		// 0->3GB
		if( map_ram(pVM,0x0,userspace_addr,0xC0000000ULL) != 0 ) {
			LOGE("Failed to map low-RAM into VM");
			return -2;
		}

		userspace_addr = mmap(0, (uint64_t)(pVM->config.ramsize-3*1024)*1024ULL*1024ULL,
			PROT_EXEC | PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if( userspace_addr == MAP_FAILED ) {
			LOGE("Failed to allocate RAM");
			return -1;
		}

		pVM->ram.pHigh = userspace_addr;
		LOGD("HighRAM = %p",pVM->ram.pHigh);

		// 4GB -> top
		if( map_ram(pVM,0x100000000ULL,userspace_addr,(uint64_t)(pVM->config.ramsize-3*1024)*1024ULL*1024ULL) != 0 ) {
			LOGE("Failed to map high-RAM into VM");
			return -2;
		}
	}

	return 0;
}

int intvm_memory_release(vm_t *pVM) {
	if( pVM == NULL ) return -1;

	if( pVM->ram.pLow != 0 ) {
		munmap(pVM->ram.pLow,pVM->config.ramsize*1024*1024);
		pVM->ram.pLow = NULL;
	}

	return 0;
}

void *intvm_memory_getguestspaceptr(vm_t *pVM,uint64_t guestaddr) {
	if( pVM == NULL ) return NULL;

	// Are we only using low-ram ?
	if( pVM->config.ramsize < (3*1024) ) {
		if( guestaddr < (pVM->config.ramsize*1024ULL*1024ULL) ) {
			return pVM->ram.pLow + guestaddr;
		}
		return NULL;
	}

	// low + high-ram
	if( guestaddr < (3*1024ULL*1024ULL*1024ULL) ) {
		return pVM->ram.pLow + guestaddr;
	} else if( guestaddr < (pVM->config.ramsize*1024ULL*1024ULL) ) {
		return pVM->ram.pLow + (guestaddr - 3*1024ULL*1024ULL*1024ULL);
	}

	return NULL;
}