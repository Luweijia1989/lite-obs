#pragma once

#include <stdint.h>
#include <memory>
#include <atomic>
#include <vector>
#include "lite-obs/lite_obs_defines.h"

#define OBS_ENCODER_CAP_DEPRECATED (1 << 0)
#define OBS_ENCODER_CAP_PASS_TEXTURE (1 << 1)

enum class obs_encoder_type {
    OBS_ENCODER_AUDIO,
    OBS_ENCODER_VIDEO
};

class lite_obs_encoder;
struct encoder_packet {
    std::shared_ptr<std::vector<uint8_t>> data;

    bool encoder_first_packet{}; /** true encoder's first output packet */

    int64_t pts{}; /**< Presentation timestamp */
    int64_t dts{}; /**< Decode timestamp */

    int32_t timebase_num{}; /**< Timebase numerator */
    int32_t timebase_den{}; /**< Timebase denominator */

    obs_encoder_type type{}; /**< Encoder type */

    bool keyframe{}; /**< Is a keyframe */

    /* ---------------------------------------------------------------- */
    /* Internal video variables (will be parsed automatically) */

    /* DTS in microseconds */
    int64_t dts_usec{};

    /* System DTS in microseconds */
    int64_t sys_dts_usec{};

    /**
     * Packet priority
     *
     * This is generally use by video encoders to specify the priority
     * of the packet.
     */
    int priority{};

    /**
     * Dropped packet priority
     *
     * If this packet needs to be dropped, the next packet must be of this
     * priority or higher to continue transmission.
     */
    int drop_priority{};

    /** Audio track index (used with outputs) */
    size_t track_idx{};

    /** Encoder from which the track originated from */
    std::weak_ptr<lite_obs_encoder> encoder{};
};

#define MICROSECOND_DEN 1000000
static inline int64_t packet_dts_usec(std::shared_ptr<encoder_packet> packet)
{
    return packet->dts * MICROSECOND_DEN / packet->timebase_den;
}

/** Encoder input frame */
struct encoder_frame {
    /** Data for the frame/audio */
    uint8_t *data[MAX_AV_PLANES]{};

    /** size of each plane */
    uint32_t linesize[MAX_AV_PLANES]{};

    /** Number of frames (audio only) */
    uint32_t frames{};

    /** Presentation timestamp */
    int64_t pts{};
};
