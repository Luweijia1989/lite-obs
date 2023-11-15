#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lite_obs_output_callbak
{
    void (*start)(void *);
    void (*stop)(int code, const char *msg, void *);
    void (*starting)(void *);
    void (*stopping)(void *);
    void (*activate)(void *);
    void (*deactivate)(void *);
    void (*connected)(void *);
    void (*reconnect)(void *);
    void (*reconnect_success)(void *);
    void (*first_media_packet)(void *);

    void *opaque;
} lite_obs_output_callbak;

#ifdef __cplusplus
}
#endif
