#ifndef PLATFORM_H
#define PLATFORM_H

#include <cstdint>

enum os_event_type {
    OS_EVENT_TYPE_AUTO,
    OS_EVENT_TYPE_MANUAL,
};

struct os_event_data;
struct os_sem_data;
typedef struct os_event_data os_event_t;
typedef struct os_sem_data os_sem_t;

int os_event_init(os_event_t **event, enum os_event_type type);
void os_event_destroy(os_event_t *event);
int os_event_wait(os_event_t *event);
int os_event_timedwait(os_event_t *event, unsigned long milliseconds);
int os_event_try(os_event_t *event);
int os_event_signal(os_event_t *event);
void os_event_reset(os_event_t *event);

int os_sem_init(os_sem_t **sem, int value);
void os_sem_destroy(os_sem_t *sem);
int os_sem_post(os_sem_t *sem);
int os_sem_wait(os_sem_t *sem);

int64_t os_gettime_ns();
void os_sleep_ms(uint32_t duration);
bool os_sleepto_ns(uint64_t time_target);
void os_breakpoint(void);

#endif // PLATFORM_H
