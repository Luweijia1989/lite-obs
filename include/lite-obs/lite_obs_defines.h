#pragma once

#define MAX_AV_PLANES 8

enum audio_format {
    AUDIO_FORMAT_UNKNOWN,

    AUDIO_FORMAT_U8BIT,
    AUDIO_FORMAT_16BIT,
    AUDIO_FORMAT_32BIT,
    AUDIO_FORMAT_FLOAT,

    AUDIO_FORMAT_U8BIT_PLANAR,
    AUDIO_FORMAT_16BIT_PLANAR,
    AUDIO_FORMAT_32BIT_PLANAR,
    AUDIO_FORMAT_FLOAT_PLANAR,
};

enum speaker_layout {
    SPEAKERS_UNKNOWN,     /**< Unknown setting, fallback is stereo. */
    SPEAKERS_MONO,        /**< Channels: MONO */
    SPEAKERS_STEREO,      /**< Channels: FL, FR */
    SPEAKERS_2POINT1,     /**< Channels: FL, FR, LFE */
    SPEAKERS_4POINT0,     /**< Channels: FL, FR, FC, RC */
    SPEAKERS_4POINT1,     /**< Channels: FL, FR, FC, LFE, RC */
    SPEAKERS_5POINT1,     /**< Channels: FL, FR, FC, LFE, RL, RR */
    SPEAKERS_7POINT1 = 8, /**< Channels: FL, FR, FC, LFE, RL, RR, SL, SR */
};


enum video_format {
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

enum video_colorspace {
    VIDEO_CS_DEFAULT,
    VIDEO_CS_601,
    VIDEO_CS_709,
};

enum video_range_type {
    VIDEO_RANGE_DEFAULT,
    VIDEO_RANGE_PARTIAL,
    VIDEO_RANGE_FULL
};

enum order_movement {
    MOVE_UP,
    MOVE_DOWN,
    MOVE_TOP,
    MOVE_BOTTOM,
};

enum source_type {
    SOURCE_VIDEO = 1 << 0,
    SOURCE_AUDIO = 1 << 1,
    SOURCE_ASYNC = 1 << 2,
    SOURCE_ASYNCVIDEO = SOURCE_ASYNC | SOURCE_VIDEO,
    SOURCE_AUDIOVIDEO = SOURCE_AUDIO | SOURCE_ASYNCVIDEO,
};

enum source_aspect_ratio_mode {
    IGNORE_ASPECT_RATIO,
    KEEP_ASPECT_RATIO,
    KEEP_ASPECT_RATIO_BY_EXPANDING,
};

enum output_type {
    rtmp,
    srt,
    file,
    android_aoa,
    iOS_usb
};
