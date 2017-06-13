#pragma once

#include <stdint.h>

int devices_init(void);

int devices_io_out(uint16_t port,int datalen,void *pData);
int devices_io_in(uint16_t port,int datalen,void *pData);
