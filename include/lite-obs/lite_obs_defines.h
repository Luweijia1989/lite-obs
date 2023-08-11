/******************************************************************************
    Copyright (C) 2013-2014 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

/** Maximum number of source channels for output and per display */
#define MAX_CHANNELS 64

/** Audio max planes */
#define MAX_AV_PLANES 8

#define OBS_ALIGN_CENTER (0)
#define OBS_ALIGN_LEFT (1 << 0)
#define OBS_ALIGN_RIGHT (1 << 1)
#define OBS_ALIGN_TOP (1 << 2)
#define OBS_ALIGN_BOTTOM (1 << 3)

#define MODULE_SUCCESS 0
#define MODULE_ERROR -1
#define MODULE_FILE_NOT_FOUND -2
#define MODULE_MISSING_EXPORTS -3
#define MODULE_INCOMPATIBLE_VER -4

#define OBS_OUTPUT_SUCCESS 0
#define OBS_OUTPUT_BAD_PATH -1
#define OBS_OUTPUT_CONNECT_FAILED -2
#define OBS_OUTPUT_INVALID_STREAM -3
#define OBS_OUTPUT_ERROR -4
#define OBS_OUTPUT_DISCONNECTED -5
#define OBS_OUTPUT_UNSUPPORTED -6
#define OBS_OUTPUT_NO_SPACE -7
#define OBS_OUTPUT_ENCODE_ERROR -8

#define OBS_VIDEO_SUCCESS 0
#define OBS_VIDEO_FAIL -1
#define OBS_VIDEO_NOT_SUPPORTED -2
#define OBS_VIDEO_INVALID_PARAM -3
#define OBS_VIDEO_CURRENTLY_ACTIVE -4
#define OBS_VIDEO_MODULE_NOT_FOUND -5

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

enum class audio_format {
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

enum class speaker_layout {
    SPEAKERS_UNKNOWN,     /**< Unknown setting, fallback is stereo. */
    SPEAKERS_MONO,        /**< Channels: MONO */
    SPEAKERS_STEREO,      /**< Channels: FL, FR */
    SPEAKERS_2POINT1,     /**< Channels: FL, FR, LFE */
    SPEAKERS_4POINT0,     /**< Channels: FL, FR, FC, RC */
    SPEAKERS_4POINT1,     /**< Channels: FL, FR, FC, LFE, RC */
    SPEAKERS_5POINT1,     /**< Channels: FL, FR, FC, LFE, RL, RR */
    SPEAKERS_7POINT1 = 8, /**< Channels: FL, FR, FC, LFE, RL, RR, SL, SR */
};
