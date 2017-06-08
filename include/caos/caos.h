#pragma once

/** @file
 * This file contains OS Abstraction functions.
 */

#include "caos/types.h"

/*
 * Some function-name translations for various systems
 */
#ifndef _WIN32
#include <unistd.h>
#ifdef linux
#include <malloc.h>
#endif
#endif
#ifdef _WIN32
#include <Winsock2.h>
#ifndef strdup
#define strdup _strdup
#endif
#ifndef strcasecmp
#define strcasecmp _strcmpi
#endif
#ifndef strndup
#define strndup(x,y) strncpy(calloc(1,y+1),x,y)
#endif
#ifndef snprintf
#define snprintf _snprintf
#endif
#ifndef memalign
#define memalign(x,y) malloc(x*y)
#endif
#ifndef strtoll
#define strtoll _strtoi64
#endif
#ifndef vsnprintf
#define vsnprintf _vsnprintf
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
#define strdup _strdup
#define execve _execve
#define getcwd _getcwd

char *strtok_r(char *str, const char *delim, char **saveptr);

//#define INLINE _inline
#define INLINE

#elif defined(__linux__) || defined(__APPLE__) || defined(ANDROID)

#define INLINE inline
//#define INLINE

#endif

/** Initializes OSAL.
 * Some systems needs to have sub-systems initialized before the rest
 * of OSALs functions can be expected to run normally, so you should
 * call this function before any other OSAL function.
 * @returns 0 on success
 */
int   CAOS_Init(void);
/** Finalizes OSAL.
 * When you are done using OSAL, this function should be called to release
 * the memory and modules allocated and loaded by CAOS_Init().
 */
void  CAOS_Fini(void);
/** Sleeps for a specified number of milliseconds.
 * Note: Due to scheduling and load of the underlaying OS, this function
 * may return after a greater number of milliseconds than requested. It
 * should be very close though, but should not be considered hard-realtime.
 * @param iMillis Number of milliseconds to sleep
 */
void  CAOS_Sleep(int iMillis);

// Semaphores
/** Creates a semaphore with an initial value.
 * @param initialValue The initial value of the semaphore (0 if you don't know what this means)
 * @returns NULL on error, or a handle to the semaphore
 */
void *CAOS_SemaphoreCreate(unsigned int initialValue);
/** Destroys a semaphore.
 * This destroys a semaphore (as created with CAOS_SemaphoreCreate()), and releases
 * the associated memory.
 * Note: As with all semaphores, make sure nothing is "waiting" on the semaphore while
 * destroying it.
 * @param sem The handle to the semaphore
 */
void  CAOS_SemaphoreDestroy(void *sem);
/** Posts/Unlocks the semaphore.
 * @param sem The handle to the semaphore
 */
void  CAOS_SemaphorePost(void *sem);
/** Waits/Locks the semaphore.
 * @param sem The handle to the semaphore
 */
void  CAOS_SemaphoreWait(void *sem);
/** Waits for a semaphore, but with a timeout.
 * @param sem The handle to the semaphore
 * @param iMillis The timeout in milliseconds
 * @returns 0 on successful wait, !0 if timeout expired
 */
int   CAOS_SemaphoreTimedWait(void *sem, int iMillis);

/** Creates a thread, and schedules it to run.
 * @param fn The entrypoint for the thread
 * @param data User-data pointer to be passed to the threads entrypoint
 * @returns NULL on failure, handle to the thread on success
 */
void *CAOS_ThreadCreate(void *(*fn)(void *), void *data);
/** Kills off a previously created thread.
 * Note: This is a last-ditch function. Use CAOS_ThreadJoin() when possible.
 * @param thread The handle of the thread to be killed
 */
void  CAOS_ThreadKill(void *thread);
/** "Joins" a thread.
 * Joining a thread basically means "wait here until the thread has finished executing"
 * This is a cleaner way to make sure your threads are cleaned up before exiting.
 * @param thread The handle of the thread to be joined
 */
void  CAOS_ThreadJoin(void *thread);

/** Gets the currently set errorcode.
 * OSAL uses errorcodes for more detailed error-detecting. It's similar to the
 * use of "errno" in POSIX applications.
 * The error-codes are defined in error.h, and are prefixed by PAERROR_
 * NOTE: The error-codes are thread-local
 * @returns The currently set error-code (which can be PAERROR_NONE, if everything succeeded)
 */
uint32 CAOS_ErrorGet(void);
/** Sets an error-code.
 * This function should only be used internally within OSAL
 * @param err The error-code to be set.
 */
void   CAOS_ErrorSet(uint32 err);

/** Creates a mutex.
 * @returns A handle to a created mutex, or NULL on failure
 */
void *CAOS_MutexCreate(void);
/** Destroy a mutex.
 * Destroys a mutex that was created by CAOS_MutexCreate(), and frees the associated memory.
 * Note: Make sure no thread uses the mutex handle while or after destroying it
 * @param mutex The mutex to destroy
 */
void  CAOS_MutexDestroy(void *mutex);
/** Lock a mutex.
 * @param mutex The mutex to lock
 */
void  CAOS_MutexLock(void *mutex);
/** Unlock a mutex.
 * @param mutex The mutex to unlock
 */
void  CAOS_MutexUnlock(void *mutex);

/** Output a debug string.
 * This function will output a string to the platforms debug console or log.
 * It does not accept formatting. For formatting, use the LOG() functions in log.h
 * @param pzString The string to be outputted
 */
void CAOS_OutputDebugString(const char *pzString);

/** Get the current time with milli-second accuracy.
 * NOTE: This does not have a set epoc, meaning, you are not guaranteed that it
 * will start from 0.
 * @returns The current time in milli-seconds
 */
unsigned long long CAOS_GetTimeMS(void);

/** Get the current time with micro-second accuracy.
 * NOTE: This does not have a set epoc, meaning, you are not guaranteed that it
 * will start from 0.
 * @returns The current time in micro-seconds
 */
unsigned long long CAOS_GetTimeUS(void);

int CAOS_MapFile(const char *pzFilename,void **ppMapping,uint32 *puFilesize);

#if defined(__linux__) || defined(__APPLE__)
#include <sys/ipc.h>

typedef struct {
  int    hdl;
  void  *pAddr;
  char  *pzID;
} shmhandle_t;
#endif

shmhandle_t *CAOS_SHM_Create(const char *pzID,void **pAddr,int size);
void         CAOS_SHM_Destroy(shmhandle_t *hdl);
int          CAOS_SHM_Map(shmhandle_t *hdl,void **pAddr,int size);
void         CAOS_SHM_Unmap(shmhandle_t *hdl);


#ifdef __cplusplus
}
#endif
