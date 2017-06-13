#include <string.h>

#include "caos/log.h"

#include "vm/vm_internal.h"

typedef struct mp_floating_pointer_structure {
    char signature[4];
    uint32_t configuration_table;
    uint8_t length; // In 16 bytes (e.g. 1 = 16 bytes, 2 = 32 bytes)
    uint8_t mp_specification_revision;
    uint8_t checksum; // This value should make all bytes in the table equal 0 when added together
    uint8_t default_configuration; // If this is not zero then configuration_table should be 
                                   // ignored and a default configuration should be loaded instead
    uint32_t features; // If bit 7 is then the IMCR is present and PIC mode is being used, otherwise 
                       // virtual wire mode is; all other bits are reserved
} mp_fps_t;

typedef struct mp_configuration_table {
    char signature[4]; // "PCMP"
    uint16_t length;
    uint8_t mp_specification_revision;
    uint8_t checksum; // Again, the byte should be all bytes in the table add up to 0
    char oem_id[8];
    char product_id[12];
    uint32_t oem_table;
    uint16_t oem_table_size;
    uint16_t entry_count; // This value represents how many entries are following this table
    uint32_t lapic_address; // This is the memory mapped address of the local APICs 
    uint16_t extended_table_length;
    uint8_t extended_table_checksum;
    uint8_t reserved;
} mp_ct_t;

typedef struct entry_processor {
    uint8_t type; // Always 0
    uint8_t local_apic_id;
    uint8_t local_apic_version;
    uint8_t flags; // If bit 0 is clear then the processor must be ignored
                   // If bit 1 is set then the processor is the bootstrap processor
    uint32_t signature;
    uint32_t feature_flags;
    uint64_t reserved;
} cpuentry_t;

typedef struct entry_io_apic {
    uint8_t type; // Always 2
    uint8_t id;
    uint8_t version;
    uint8_t flags; // If bit 0 is set then the entry should be ignored
    uint32_t address; // The memory mapped address of the IO APIC is memory
} ioapicentry_t;

static unsigned int sumdata(void *p,int len) {
	unsigned int r = 0;
	int i;
	uint8_t *pD = (uint8_t*)p;

	for(i=0;i<len;i++) {
		r += pD[i];
	}
	return r;
}

static int create_mps_table(vm_t *pVM) {
	cpuentry_t *pCPU;
	unsigned int csum;
	uint8_t *pEntry;
	int i;

	mp_fps_t *pFPS = (mp_fps_t*)intvm_memory_getguestspaceptr(pVM,0xF0000);
	memset(pFPS,0,sizeof(mp_fps_t));

	// Signature
	pFPS->signature[0] = '_';
	pFPS->signature[1] = 'M';
	pFPS->signature[2] = 'P';
	pFPS->signature[3] = '_';
	pFPS->length = 1; // 16 bytes
	pFPS->mp_specification_revision = 4;
	pFPS->configuration_table = 0xF0010;

	csum = sumdata(pFPS,sizeof(mp_fps_t));
	csum = 0x100 - (csum & 0xFF);
	pFPS->checksum = csum;

	mp_ct_t *pCT = (mp_ct_t*)intvm_memory_getguestspaceptr(pVM,0xF0010);
	memset(pCT,0,sizeof(mp_ct_t));

	// Signature
	pCT->signature[0] = 'P';
	pCT->signature[1] = 'C';
	pCT->signature[2] = 'M';
	pCT->signature[3] = 'P';
	pCT->length = sizeof(mp_ct_t);
	pCT->mp_specification_revision = 4;
	pCT->lapic_address = 0xFEE00000;
	strcpy(pCT->oem_id,"KVMOEM");
	strcpy(pCT->product_id,"KVMPRODUCT");
	//pCT->entry_count = pVM->numcpus;

	pEntry = intvm_memory_getguestspaceptr(pVM,0xF0010 + sizeof(mp_ct_t));
	for(i=0;i<pVM->config.numcpus;i++) {
		pCPU = (cpuentry_t*)pEntry;
		pCPU->type = 0;
		pCPU->local_apic_id = i;
		pCPU->local_apic_version = 0x14; // TODO: Get this from KVM or the host
		pCPU->flags = 1; // Enabled
		if (i == 0) pCPU->flags |= (1<<1); // BP
		pCPU->signature = 0;

		pEntry += 20; // CPU entries are 20 bytes
		pCT->length += 20;
		pCT->entry_count++;
	}

	ioapicentry_t *pIOAPIC;

	for(i=0;i<1;i++) {
		pIOAPIC = (ioapicentry_t*)pEntry;
		pIOAPIC->type = 2; // IO-APIC
		pIOAPIC->id   = i;
		pIOAPIC->version = 0x17;
		pIOAPIC->flags = 1;
		pIOAPIC->address = 0xfec00000;
		pEntry += 8;
		pCT->length += 8;
		pCT->entry_count++;
	}

	csum = sumdata(pCT,pCT->length);
	csum = 0x100 - (csum & 0xFF);
	pCT->checksum = csum;


	return 0;
}

int intvm_biostables_setup(vm_t *pVM) {

	if( create_mps_table(pVM) != 0 ) {
		LOGE("Failed to create MPS table");
		return -1;
	}

	return 0;
}
