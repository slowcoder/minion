#pragma once

#include <stdint.h>

struct vm *pVM;

int devices_init(struct vm *pVM);

int devices_io_out(uint16_t port,int datalen,void *pData);
int devices_io_in(uint16_t port,int datalen,void *pData);
