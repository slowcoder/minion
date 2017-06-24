#pragma once

#include <stdint.h>

struct vm *pVM;

int devices_init(struct vm *pVM);
int devices_destroy(struct vm *pVM);

int devices_io_out(uint16_t port,int datalen,void *pData);
int devices_io_in(uint16_t port,int datalen,void *pData);

int devices_mmio_out(uint64_t addr,int datalen,void *pData);
int devices_mmio_in(uint64_t addr,int datalen,void *pData);

struct disk;
int devices_disk_attach(struct vm *pVM,struct disk *pDisk);
