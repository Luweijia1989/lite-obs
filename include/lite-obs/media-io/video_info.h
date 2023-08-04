#pragma once

#include "lite-obs/lite_obs_defines.h"

static inline bool format_is_yuv(video_format format)
{
    switch (format) {
    case video_format::VIDEO_FORMAT_I420:
    case video_format::VIDEO_FORMAT_NV12:
    case video_format::VIDEO_FORMAT_I422:
    case video_format::VIDEO_FORMAT_YVYU:
    case video_format::VIDEO_FORMAT_YUY2:
    case video_format::VIDEO_FORMAT_UYVY:
    case video_format::VIDEO_FORMAT_I444:
    case video_format::VIDEO_FORMAT_I40A:
    case video_format::VIDEO_FORMAT_I42A:
    case video_format::VIDEO_FORMAT_YUVA:
    case video_format::VIDEO_FORMAT_AYUV:
        return true;
    case video_format::VIDEO_FORMAT_NONE:
    case video_format::VIDEO_FORMAT_RGBA:
    case video_format::VIDEO_FORMAT_BGRA:
    case video_format::VIDEO_FORMAT_BGRX:
    case video_format::VIDEO_FORMAT_Y800:
    case video_format::VIDEO_FORMAT_BGR3:
        return false;
    }

    return false;
}

enum class video_colorspace {
    VIDEO_CS_DEFAULT,
    VIDEO_CS_601,
    VIDEO_CS_709,
};

enum class video_range_type {
    VIDEO_RANGE_DEFAULT,
    VIDEO_RANGE_PARTIAL,
    VIDEO_RANGE_FULL
};

enum class video_scale_type {
    VIDEO_SCALE_DEFAULT,
    VIDEO_SCALE_POINT,
    VIDEO_SCALE_FAST_BILINEAR,
    VIDEO_SCALE_BILINEAR,
    VIDEO_SCALE_BICUBIC,
};
