#include "lite-obs/util/log.h"

#ifdef _DEBUG
static int log_output_level = LOG_DEBUG;
#else
static int log_output_level = LOG_INFO;
#endif

static void def_log_handler(int log_level, const char *out)
{
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

static log_handler_t log_handler = def_log_handler;

void base_set_log_handler(log_handler_t handler)
{
    if (!handler)
        handler = def_log_handler;

    log_handler = handler;
}

void blog(int log_level, const char *format, ...)
{
    va_list args;

    va_start(args, format);

    char out[4096];
    vsnprintf(out, sizeof(out), format, args);
    log_handler(log_level, out);

    va_end(args);
}
