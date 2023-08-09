#include "time_util.h"

#if defined _WIN32
#include <windows.h>

static int have_clockfreq = 0;
static LARGE_INTEGER clock_freq;

static inline uint64_t get_clockfreq(void)
{
    if (!have_clockfreq) {
        QueryPerformanceFrequency(&clock_freq);
        have_clockfreq = 1;
    }

    return clock_freq.QuadPart;
}

uint64_t get_system_time_ns()
{
    LARGE_INTEGER current_time;
    double time_val;

    QueryPerformanceCounter(&current_time);
    time_val = (double)current_time.QuadPart;
    time_val *= 1000000000.0;
    time_val /= (double)get_clockfreq();

    return (uint64_t)time_val;
}

#else

#include <time.h>

uint64_t get_system_time_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec);
}

#endif
