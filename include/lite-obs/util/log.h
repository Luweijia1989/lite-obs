#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>

enum {
    /**
     * Use if there's a problem that can potentially affect the program,
     * but isn't enough to require termination of the program.
     *
     * Use in creation functions and core subsystem functions.  Places that
     * should definitely not fail.
     */
    LOG_ERROR = 100,

    /**
     * Use if a problem occurs that doesn't affect the program and is
     * recoverable.
     *
     * Use in places where failure isn't entirely unexpected, and can
     * be handled safely.
     */
    LOG_WARNING = 200,

    /**
     * Informative message to be displayed in the log.
     */
    LOG_INFO = 300,

    /**
     * Debug message to be used mostly by developers.
     */
    LOG_DEBUG = 400
};

#ifdef _DEBUG
static int log_output_level = LOG_DEBUG;
#else
static int log_output_level = LOG_INFO;
#endif

static void def_log_handler(int log_level, const char *format, va_list args)
{
    char out[4096];
    vsnprintf(out, sizeof(out), format, args);

    if (log_level <= log_output_level) {
        switch (log_level) {
        case LOG_DEBUG:
            fprintf(stdout, "debug: %s\n", out);
            fflush(stdout);
            break;

        case LOG_INFO:
            fprintf(stdout, "info: %s\n", out);
            fflush(stdout);
            break;

        case LOG_WARNING:
            fprintf(stdout, "warning: %s\n", out);
            fflush(stdout);
            break;

        case LOG_ERROR:
            fprintf(stderr, "error: %s\n", out);
            fflush(stderr);
        }
    }
}

static inline void blog(int log_level, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    def_log_handler(log_level, format, args);
    va_end(args);
}

#endif // LOG_H
