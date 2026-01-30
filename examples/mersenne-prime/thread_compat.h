#ifndef TTAK_THREAD_COMPAT_H
#define TTAK_THREAD_COMPAT_H

#ifdef _WIN32
#include <windows.h>
#include <process.h>
typedef HANDLE ttak_thread_t;
typedef DWORD  ttak_thread_id_t;
#define TTAK_THREAD_CALL WINAPI
#define ttak_thread_yield() Sleep(0)
#else
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
typedef pthread_t ttak_thread_t;
typedef pthread_t ttak_thread_id_t;
#define TTAK_THREAD_CALL
#define ttak_thread_yield() sched_yield()
#endif

typedef void* (TTAK_THREAD_CALL *ttak_thread_func_t)(void*);

static inline int ttak_thread_create(ttak_thread_t *thread, ttak_thread_func_t func, void *arg) {
#ifdef _WIN32
    *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return (*thread == NULL) ? -1 : 0;
#else
    return pthread_create(thread, NULL, func, arg);
#endif
}

static inline int ttak_thread_join(ttak_thread_t thread, void **retval) {
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    if (retval) GetExitCodeThread(thread, (LPDWORD)retval);
    CloseHandle(thread);
    return 0;
#else
    return pthread_join(thread, retval);
#endif
}

#endif // TTAK_THREAD_COMPAT_H
