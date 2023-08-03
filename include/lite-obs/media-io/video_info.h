#pragma once

enum class video_format {
    VIDEO_FORMAT_NONE,

    /* planar 420 format */
    VIDEO_FORMAT_I420, /* three-plane */
    VIDEO_FORMAT_NV12, /* two-plane, luma and packed chroma */

    /* packed 422 formats */
    VIDEO_FORMAT_YVYU,
    VIDEO_FORMAT_YUY2, /* YUYV */
    VIDEO_FORMAT_UYVY,

    /* packed uncompressed formats */
    VIDEO_FORMAT_RGBA,
    VIDEO_FORMAT_BGRA,
    VIDEO_FORMAT_BGRX,
    VIDEO_FORMAT_Y800, /* grayscale */

    /* planar 4:4:4 */
    VIDEO_FORMAT_I444,

    /* more packed uncompressed formats */
    VIDEO_FORMAT_BGR3,

    /* planar 4:2:2 */
    VIDEO_FORMAT_I422,

    /* planar 4:2:0 with alpha */
    VIDEO_FORMAT_I40A,

    /* planar 4:2:2 with alpha */
    VIDEO_FORMAT_I42A,

    /* planar 4:4:4 with alpha */
    VIDEO_FORMAT_YUVA,

    /* packed 4:4:4 with alpha */
    VIDEO_FORMAT_AYUV,
};

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
