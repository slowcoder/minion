#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "caos/log.h"

#include "hw/disks/disks.h"

#define USE_F 0

typedef struct disk {
	FILE *in;
	int fd;

	uint64_t size;
} disk_t;

struct disk *disks_open_flatfile(const char *pzFilename) {
	disk_t *pMe;

	pMe = (disk_t*)calloc(1,sizeof(disk_t));

#if USE_F
	FILE *in;

	in = fopen(pzFilename,"rb");
	if( in == NULL ) {
		free(pMe);
		return NULL;
	}
	pMe->in = in;
	pMe->size = fseeko(in,0,SEEK_END);
	fseeko(in,0,SEEK_SET);
#else
	int fd;

	fd = open(pzFilename,O_RDWR);
	if( fd == -1 ) {
		free(pMe);
		return NULL;
	}
	pMe->fd = fd;
	pMe->size = lseek(fd,0,SEEK_END);
	lseek(fd,0,SEEK_SET);
#endif

	return pMe;
}

void         disks_close(struct disk *pDisk) {
	if( pDisk == NULL ) return;

#if USE_F
	if( pDisk->in != NULL ) fclose(pDisk->in);
#else
	if( pDisk->fd != -1 ) close(pDisk->fd);
#endif

	free(pDisk);
}

int disks_get_capacity(struct disk *pDisk,uint64_t *pullCapacity) {
	if( pDisk == NULL ) return -1;
	if( pullCapacity == NULL ) return -2;

	*pullCapacity = pDisk->size;

	return 0;
}

int disks_read(struct disk *pDisk,uint64_t offset,uint64_t numbytes,void *pData) {

#if USE_F
	fseeko(pDisk->in,offset,SEEK_SET);
	fread(pData,1,numbytes,pDisk->in);
#else
	ssize_t r;

	ASSERT( (offset+numbytes) < pDisk->size,"Read outside of disk..");

	lseek(pDisk->fd,offset,SEEK_SET);
	r = read(pDisk->fd,pData,numbytes);
	ASSERT(r==numbytes,"Short read (r=%i)",r);
#endif
	return 0;
}

int disks_write(struct disk *pDisk,uint64_t offset,uint64_t numbytes,void *pData) {
#if USE_F
	fseeko(pDisk->in,offset,SEEK_SET);
	fread(pData,1,numbytes,pDisk->in);
	ASSERT(0,"Implement me");
	return -1;
#else
	ASSERT( (offset+numbytes) < pDisk->size,"Write outside of disk..");

	lseek(pDisk->fd,offset,SEEK_SET);
	write(pDisk->fd,pData,numbytes);
#endif
	return 0;

}
