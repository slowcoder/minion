#pragma once

#include <stdint.h>

struct disk *disks_open_flatfile(const char *pzFilename);
void         disks_close(struct disk *pDisk);

int disks_get_capacity(struct disk *pDisk,uint64_t *pullCapacity);
int disks_read(struct disk *pDisk,uint64_t offset,uint64_t numbytes,void *pData);
int disks_write(struct disk *pDisk,uint64_t offset,uint64_t numbytes,void *pData);
