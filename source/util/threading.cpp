#include "lite-obs/util/threading.h"
#include "lite-obs/lite_obs_platform_config.h"
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

struct os_event_data {
    std::mutex mutex;
    std::condition_variable cond;
    volatile bool signalled = false;
    bool manual = false;
};

int os_event_init(os_event_t **event, enum os_event_type type)
{
    auto data = new os_event_data;
    data->manual = (type == OS_EVENT_TYPE_MANUAL);
    data->signalled = false;
    *event = data;

    return 0;
}

void os_event_destroy(os_event_t *event)
{
    if (event)
        delete event;
}

int os_event_wait(os_event_t *event)
{
    std::unique_lock<std::mutex> lock(event->mutex);
    if (!event->signalled)
        event->cond.wait(lock);

    if (!event->manual)
        event->signalled = false;

    return 0;
}

int os_event_timedwait(os_event_t *event, unsigned long milliseconds)
{
    std::unique_lock<std::mutex> lock(event->mutex);
    if (!event->signalled) {
        event->cond.wait_for(lock, std::chrono::milliseconds(milliseconds));
    }

    if (!event->manual)
        event->signalled = false;

    return 0;
}

int os_event_try(os_event_t *event)
{
    int ret = EAGAIN;

    std::lock_guard<std::mutex> lock(event->mutex);
    if (event->signalled) {
        if (!event->manual)
            event->signalled = false;
        ret = 0;
    }

    return ret;
}

int os_event_signal(os_event_t *event)
{
    std::unique_lock<std::mutex> lock(event->mutex);
    event->cond.notify_all();
    event->signalled = true;

    return 0;
}

void os_event_reset(os_event_t *event)
{
    std::lock_guard<std::mutex> lock(event->mutex);
    event->signalled = false;
}

#if TARGET_PLATFORM == PLATFORM_WIN32
int os_sem_init(os_sem_t **sem, int value)
{
    HANDLE handle = CreateSemaphore(NULL, (LONG)value, 0x7FFFFFFF, NULL);
    if (!handle)
        return -1;

    *sem = (os_sem_t *)handle;
    return 0;
}

void os_sem_destroy(os_sem_t *sem)
{
    if (sem)
        CloseHandle((HANDLE)sem);
}

int os_sem_post(os_sem_t *sem)
{
    if (!sem)
        return -1;
    return ReleaseSemaphore((HANDLE)sem, 1, NULL) ? 0 : -1;
}

int os_sem_wait(os_sem_t *sem)
{
    DWORD ret;

    if (!sem)
        return -1;
    ret = WaitForSingleObject((HANDLE)sem, INFINITE);
    return (ret == WAIT_OBJECT_0) ? 0 : -1;
}
#elif TARGET_PLATFORM == PLATFORM_IOS || TARGET_PLATFORM == PLATFORM_MAC
#include <mach/semaphore.h>
#include <mach/task.h>
#include <mach/mach_init.h>
struct os_sem_data {
    semaphore_t sem{};
    task_t task{};
};

int os_sem_init(os_sem_t **sem, int value)
{
    semaphore_t new_sem;
    task_t task = mach_task_self();

    if (semaphore_create(task, &new_sem, 0, value) != KERN_SUCCESS)
        return -1;

    *sem = new os_sem_data;
    if (!*sem)
        return -2;

    (*sem)->sem = new_sem;
    (*sem)->task = task;
    return 0;
}

void os_sem_destroy(os_sem_t *sem)
{
    if (sem) {
        semaphore_destroy(sem->task, sem->sem);
        delete sem;
    }
}

int os_sem_post(os_sem_t *sem)
{
    if (!sem)
        return -1;
    return (semaphore_signal(sem->sem) == KERN_SUCCESS) ? 0 : -1;
}

int os_sem_wait(os_sem_t *sem)
{
    if (!sem)
        return -1;
    return (semaphore_wait(sem->sem) == KERN_SUCCESS) ? 0 : -1;
}
#else
#include <semaphore.h>
struct os_sem_data {
    sem_t sem;
};

int os_sem_init(os_sem_t **sem, int value)
{
    sem_t new_sem;
    int ret = sem_init(&new_sem, 0, value);
    if (ret != 0)
        return ret;

    *sem = new os_sem_data;
    (*sem)->sem = new_sem;
    return 0;
}

void os_sem_destroy(os_sem_t *sem)
{
    if (sem) {
        sem_destroy(&sem->sem);
        delete sem;
    }
}

int os_sem_post(os_sem_t *sem)
{
    if (!sem)
        return -1;
    return sem_post(&sem->sem);
}

int os_sem_wait(os_sem_t *sem)
{
    if (!sem)
        return -1;
    return sem_wait(&sem->sem);
}
#endif

int64_t os_gettime_ns()
{
    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

void os_sleep_ms(uint32_t duration)
{
    std::this_thread::sleep_for (std::chrono::milliseconds(duration));
}

#if TARGET_PLATFORM == PLATFORM_WIN32
#include <Windows.h>
#include <timeapi.h>
class global_task_helper {
public:
    global_task_helper() {
        timeBeginPeriod(1);
    }

    ~global_task_helper() {
        timeEndPeriod(1);
    }
};
static global_task_helper gh;
bool os_sleepto_ns(uint64_t time_target)
{
    uint64_t t = os_gettime_ns();
    uint32_t milliseconds;

    if (t >= time_target)
        return false;

    milliseconds = (uint32_t)((time_target - t) / 1000000);
    if (milliseconds > 1)
        Sleep(milliseconds - 1);

    for (;;) {
        t = os_gettime_ns();
        if (t >= time_target)
            return true;

#if 0
        Sleep(1);
#else
        Sleep(0);
#endif
    }
}
#else
bool os_sleepto_ns(uint64_t time_target)
{
    uint64_t current = os_gettime_ns();
    if (time_target < current)
        return false;

    time_target -= current;

    struct timespec req, remain;
    memset(&req, 0, sizeof(req));
    memset(&remain, 0, sizeof(remain));
    req.tv_sec = time_target / 1000000000;
    req.tv_nsec = time_target % 1000000000;

    while (nanosleep(&req, &remain)) {
        req = remain;
        memset(&remain, 0, sizeof(remain));
    }

    return true;
}
#endif

void os_breakpoint()
{
#ifdef __WIN32
    __debugbreak();
#endif
}
