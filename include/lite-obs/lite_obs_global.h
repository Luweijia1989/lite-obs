#pragma once

#define LiteOBS_VERSION_INT(major, minor, patch) \
(((major&0xff)<<16) | ((minor&0xff)<<8) | (patch&0xff))
#define LiteOBS_MAJOR 0
#define LiteOBS_MINOR 0
#define LiteOBS_MICRO 1
#define LiteOBS_VERSION LiteOBS_VERSION_INT(LiteOBS_MAJOR, LiteOBS_MINOR, LiteOBS_MICRO)
#define LiteOBS_VERSION_CHECK(a, b, c) (LiteOBS_VERSION >= LiteOBS_VERSION_INT(a, b, c))

#if defined(_WIN32)
#define LITE_OBS_EXPORT __declspec(dllexport)
#define LITE_OBS_IMPORT __declspec(dllimport)
#else
#define LITE_OBS_EXPORT __attribute__((visibility("default")))
#define LITE_OBS_IMPORT __attribute__((visibility("default")))
#endif

#ifdef BUILD_LITE_OBS_STATIC
# define LITE_OBS_API
#else
# if defined(BUILD_LITE_OBS_LIB)
#  define LITE_OBS_API LITE_OBS_EXPORT
# else
#  define LITE_OBS_API LITE_OBS_IMPORT
# endif
#endif
