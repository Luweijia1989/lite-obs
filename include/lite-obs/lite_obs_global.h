#pragma once

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
