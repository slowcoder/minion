#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#if BUILD_OS != android
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/mman.h>
#endif

#include <caos/caos.h>
#include <caos/log.h>
#include <caos/types.h>

#ifdef ANDROID
#include <android/log.h>
#endif

static pthread_key_t tls_threadinfo;

void CAOS_OutputDebugString(const char *pzString)
{
#ifdef ANDROID
	__android_log_print(ANDROID_LOG_DEBUG, "libemu", "%s", pzString);
#else
//	fprintf(stderr,"%s\n", pzString);
	printf("%s\n", pzString);
	fflush(stdout);
#endif
}

unsigned long long CAOS_GetTimeMS(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (unsigned int)((tv.tv_sec % 0x3fffff)*1000 + tv.tv_usec/1000);
}

INLINE unsigned long long CAOS_GetTimeUS(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (unsigned int)((tv.tv_sec % 0x3fffff)*1000000 + tv.tv_usec);
}

void CAOS_gethostname(char *pzStr,int len) {
	gethostname(pzStr, len);
}

/* ***** Thread stuff ***** */

typedef struct osalthread {
	pthread_t thread;
	uint32 error;
	void *(*fn)(void *);
	void *data;
} osalthread_t;

void CAOS_Sleep(int iMillis)
{
	usleep(iMillis * 1000);
}

void *CAOS_ThreadCreate(void *(*fn)(void *), void *data) {
	osalthread_t *ret;

	if (fn == NULL)
		return NULL;

	ret = (osalthread_t *)malloc(sizeof(osalthread_t));
	if (ret == NULL)
		return NULL;

	ret->fn = fn;
	ret->data = data;
	ret->error = 0;

	pthread_create(&ret->thread, NULL, fn, data);

	return ret;
}

void  CAOS_ThreadKill(void *thread) {
	osalthread_t *thd = (osalthread_t*)thread;

	if (thd == NULL)
		return;

	pthread_kill(thd->thread, SIGHUP);

	free(thd);
}

void  CAOS_ThreadJoin(void *thread) {
	osalthread_t *thd = (osalthread_t*)thread;

	if (thd == NULL)
		return;

	while( pthread_join(thd->thread, NULL) != 0 ) {}

	free(thd);
}

void *CAOS_MutexCreate()
{
	pthread_mutex_t *mutex =
			(pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(mutex, NULL);
	return mutex;
}

void  CAOS_MutexDestroy(void *mutex)
{
	pthread_mutex_destroy((pthread_mutex_t *)mutex);

	free(mutex);
}

void  CAOS_MutexLock(void *mutex)
{
	pthread_mutex_lock((pthread_mutex_t *)mutex);
}

void  CAOS_MutexUnlock(void *mutex)
{
	pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

void *CAOS_SemaphoreCreate(unsigned int initialValue)
{
	sem_t *sem = (sem_t *)malloc(sizeof(sem_t));
	sem_init(sem, 0, initialValue);
	return sem;
}

void CAOS_SemaphorePost(void *sem)
{
	sem_post((sem_t *)sem);
}

void CAOS_SemaphoreWait(void *sem)
{
	sem_wait((sem_t *)sem);
}

int  CAOS_SemaphoreTimedWait(void *sem, int iMillis)
{
	struct timespec ts;
	struct timespec dts;
	struct timespec sts;
	int s;

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
		return -1;

	dts.tv_sec = iMillis / 1000;
	dts.tv_nsec = (iMillis % 1000) * 1000000;
	sts.tv_sec = ts.tv_sec + dts.tv_sec + (dts.tv_nsec + ts.tv_nsec) / 1000000000;
	sts.tv_nsec = (dts.tv_nsec + ts.tv_nsec) % 1000000000;

	while ((s = sem_timedwait((sem_t *)sem, &sts)) == -1 && errno == EINTR)
		continue;

	if (s == -1 && errno != ETIMEDOUT)
		LOGE("Semaphore error: %s", strerror(errno));
	return s;
}

void CAOS_SemaphoreDestroy(void *sem)
{
	sem_destroy((sem_t *)sem);
	free(sem);
}

static osalthread_t gOSALThread;

int CAOS_Init(void)
{
	gOSALThread.thread = pthread_self();
	gOSALThread.error = 0;

	pthread_key_create(&tls_threadinfo, NULL);

	pthread_setspecific(tls_threadinfo, &gOSALThread);

	return 0;
}

void  CAOS_Fini(void)
{
	pthread_key_delete(tls_threadinfo);
}

#if BUILD_OS != android
int CAOS_MapFile(const char *pzFilename,void **ppMapping,uint32 *puFilesize) {
	int fd;
	struct stat sb;
	void *p;

	fd = open(pzFilename,O_RDONLY);
	if( fd < 0 )
	  return -1;

	fstat(fd, &sb);

	p = mmap(NULL,sb.st_size,PROT_READ,MAP_PRIVATE,fd,0);
	if( p == NULL ) {
	  perror("Failed to map ROM:");
	  return -1;
	}

	*puFilesize = sb.st_size;
	*ppMapping = p;

	return 0;
}

// Shared memory
shmhandle_t *CAOS_SHM_Create(const char *pzID,void **pAddr,int size) {
	int r;
	shmhandle_t *ret;

	ret = (shmhandle_t*)calloc(1,sizeof(shmhandle_t));

	ret->hdl = shm_open(pzID,O_CREAT  | O_RDWR,0777);
	if( ret->hdl < 0 ) {
		perror("Failed to create SHM area");
		free(ret);
		return NULL;
	}
	r = ftruncate(ret->hdl, size);
	if( r != 0 ) {
		free(ret);
		return NULL;
	}

	ret->pzID = strdup(pzID);

	if( pAddr != 0 ) {
		CAOS_SHM_Map(ret,pAddr,size);
	}

  return ret;
}

void         CAOS_SHM_Destroy(shmhandle_t *hdl) {
  if( shm_unlink(hdl->pzID) != 0 ) {
    perror("Failed to release SHM area");
  }

  if(hdl->pzID != NULL)
    free(hdl->pzID);

  free(hdl);
}

int CAOS_SHM_Map(shmhandle_t *hdl,void **pAddr,int size) {
  void *pMap;

  pMap = mmap(NULL,size,PROT_READ | PROT_WRITE,
	      MAP_SHARED,
	      hdl->hdl,
	      0);

  if( pMap == MAP_FAILED ) {
    LOG("Failed to map SHM area");
    perror("Failed to map SHM area");
    return -1;
  }

  hdl->pAddr = pMap;

  if( pAddr != NULL )
    *pAddr = pMap;
  return 0;
}

void         CAOS_SHM_Unmap(shmhandle_t *hdl) {
	if( hdl->pAddr != NULL ) {
		munmap(hdl->pAddr,0);
	}
}
#endif
