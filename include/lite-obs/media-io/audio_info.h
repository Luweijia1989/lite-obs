#pragma once

#include <stdint.h>
#include "lite-obs/util/util_uint128.h"

#define MAX_AUDIO_MIXES 6
#define MAX_AUDIO_CHANNELS 8
#define AUDIO_OUTPUT_FRAMES 1024

#define TOTAL_AUDIO_SIZE                                              \
    (MAX_AUDIO_MIXES * MAX_AUDIO_CHANNELS * AUDIO_OUTPUT_FRAMES * \
    sizeof(float))

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

/**
 * The speaker layout describes where the speakers are located in the room.
 * For OBS it dictates:
 *  *  how many channels are available and
 *  *  which channels are used for which speakers.
 *
 * Standard channel layouts where retrieved from ffmpeg documentation at:
 *     https://trac.ffmpeg.org/wiki/AudioChannelManipulation
 */
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

static inline uint32_t get_audio_channels(speaker_layout speakers)
{
    switch (speakers) {
    case speaker_layout::SPEAKERS_MONO:
        return 1;
    case speaker_layout::SPEAKERS_STEREO:
        return 2;
    case speaker_layout::SPEAKERS_2POINT1:
        return 3;
    case speaker_layout::SPEAKERS_4POINT0:
        return 4;
    case speaker_layout::SPEAKERS_4POINT1:
        return 5;
    case speaker_layout::SPEAKERS_5POINT1:
        return 6;
    case speaker_layout::SPEAKERS_7POINT1:
        return 8;
    case speaker_layout::SPEAKERS_UNKNOWN:
        return 0;
    }

    return 0;
}

static inline size_t get_audio_bytes_per_channel(audio_format format)
{
    switch (format) {
    case audio_format::AUDIO_FORMAT_U8BIT:
    case audio_format::AUDIO_FORMAT_U8BIT_PLANAR:
        return 1;

    case audio_format::AUDIO_FORMAT_16BIT:
    case audio_format::AUDIO_FORMAT_16BIT_PLANAR:
        return 2;

    case audio_format::AUDIO_FORMAT_FLOAT:
    case audio_format::AUDIO_FORMAT_FLOAT_PLANAR:
    case audio_format::AUDIO_FORMAT_32BIT:
    case audio_format::AUDIO_FORMAT_32BIT_PLANAR:
        return 4;

    case audio_format::AUDIO_FORMAT_UNKNOWN:
        return 0;
    }

    return 0;
}

static inline bool is_audio_planar(audio_format format)
{
    switch (format) {
    case audio_format::AUDIO_FORMAT_U8BIT:
    case audio_format::AUDIO_FORMAT_16BIT:
    case audio_format::AUDIO_FORMAT_32BIT:
    case audio_format::AUDIO_FORMAT_FLOAT:
        return false;

    case audio_format::AUDIO_FORMAT_U8BIT_PLANAR:
    case audio_format::AUDIO_FORMAT_FLOAT_PLANAR:
    case audio_format::AUDIO_FORMAT_16BIT_PLANAR:
    case audio_format::AUDIO_FORMAT_32BIT_PLANAR:
        return true;

    case audio_format::AUDIO_FORMAT_UNKNOWN:
        return false;
    }

    return false;
}

static inline size_t get_audio_planes(audio_format format,
                                      speaker_layout speakers)
{
    return (is_audio_planar(format) ? get_audio_channels(speakers) : 1);
}

static inline size_t get_audio_size(audio_format format,
                                    speaker_layout speakers,
                                    uint32_t frames)
{
    bool planar = is_audio_planar(format);

    return (planar ? 1 : get_audio_channels(speakers)) *
            get_audio_bytes_per_channel(format) * frames;
}

static inline size_t get_audio_frames(audio_format format,
                                      speaker_layout speakers,
                                      uint32_t data)
{
    bool planar = is_audio_planar(format);

    return data/((planar ? 1 : get_audio_channels(speakers)) *
                 get_audio_bytes_per_channel(format));
}

static inline uint64_t audio_frames_to_ns(size_t sample_rate, uint64_t frames)
{
    util_uint128_t val;
    val = util_mul64_64(frames, 1000000000ULL);
    val = util_div128_32(val, (uint32_t)sample_rate);
    return val.low;
}

static inline uint64_t ns_to_audio_frames(size_t sample_rate, uint64_t frames)
{
    util_uint128_t val;
    val = util_mul64_64(frames, sample_rate);
    val = util_div128_32(val, 1000000000);
    return val.low;
}
