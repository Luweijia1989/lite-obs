#pragma once

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}
#include "lite-obs/lite_obs_defines.h"

static inline int64_t rescale_ts(int64_t val, AVCodecContext *context,
                                 AVRational new_base)
{
    return av_rescale_q_rnd(val, context->time_base, new_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
}

static inline AVPixelFormat lite_obs_to_ffmpeg_video_format(video_format format)
{
    switch (format) {
    case video_format::VIDEO_FORMAT_I444:
        return AV_PIX_FMT_YUV444P;
    case video_format::VIDEO_FORMAT_I420:
        return AV_PIX_FMT_YUV420P;
    case video_format::VIDEO_FORMAT_NV12:
        return AV_PIX_FMT_NV12;
    case video_format::VIDEO_FORMAT_YUY2:
        return AV_PIX_FMT_YUYV422;
    case video_format::VIDEO_FORMAT_UYVY:
        return AV_PIX_FMT_UYVY422;
    case video_format::VIDEO_FORMAT_RGBA:
        return AV_PIX_FMT_RGBA;
    case video_format::VIDEO_FORMAT_BGRA:
        return AV_PIX_FMT_BGRA;
    case video_format::VIDEO_FORMAT_BGRX:
        return AV_PIX_FMT_BGRA;
    case video_format::VIDEO_FORMAT_Y800:
        return AV_PIX_FMT_GRAY8;
    case video_format::VIDEO_FORMAT_BGR3:
        return AV_PIX_FMT_BGR24;
    case video_format::VIDEO_FORMAT_I422:
        return AV_PIX_FMT_YUV422P;
    case video_format::VIDEO_FORMAT_I40A:
        return AV_PIX_FMT_YUVA420P;
    case video_format::VIDEO_FORMAT_I42A:
        return AV_PIX_FMT_YUVA422P;
    case video_format::VIDEO_FORMAT_YUVA:
        return AV_PIX_FMT_YUVA444P;
    case video_format::VIDEO_FORMAT_NONE:
    case video_format::VIDEO_FORMAT_YVYU:
    case video_format::VIDEO_FORMAT_AYUV:
        /* not supported by FFmpeg */
        return AV_PIX_FMT_NONE;
    }

    return AV_PIX_FMT_NONE;
}

static inline video_format ffmpeg_to_lite_obs_video_format(AVPixelFormat format)
{
    switch (format) {
    case AV_PIX_FMT_YUV444P:
        return video_format::VIDEO_FORMAT_I444;
    case AV_PIX_FMT_YUV420P:
        return video_format::VIDEO_FORMAT_I420;
    case AV_PIX_FMT_NV12:
        return video_format::VIDEO_FORMAT_NV12;
    case AV_PIX_FMT_YUYV422:
        return video_format::VIDEO_FORMAT_YUY2;
    case AV_PIX_FMT_UYVY422:
        return video_format::VIDEO_FORMAT_UYVY;
    case AV_PIX_FMT_RGBA:
        return video_format::VIDEO_FORMAT_RGBA;
    case AV_PIX_FMT_BGRA:
        return video_format::VIDEO_FORMAT_BGRA;
    case AV_PIX_FMT_GRAY8:
        return video_format::VIDEO_FORMAT_Y800;
    case AV_PIX_FMT_BGR24:
        return video_format::VIDEO_FORMAT_BGR3;
    case AV_PIX_FMT_YUV422P:
        return video_format::VIDEO_FORMAT_I422;
    case AV_PIX_FMT_YUVA420P:
        return video_format::VIDEO_FORMAT_I40A;
    case AV_PIX_FMT_YUVA422P:
        return video_format::VIDEO_FORMAT_I42A;
    case AV_PIX_FMT_YUVA444P:
        return video_format::VIDEO_FORMAT_YUVA;
    case AV_PIX_FMT_NONE:
    default:
        return video_format::VIDEO_FORMAT_NONE;
    }
}

static inline audio_format convert_ffmpeg_sample_format(AVSampleFormat format)
{
    switch ((uint32_t)format) {
    case AV_SAMPLE_FMT_U8:
        return audio_format::AUDIO_FORMAT_U8BIT;
    case AV_SAMPLE_FMT_S16:
        return audio_format::AUDIO_FORMAT_16BIT;
    case AV_SAMPLE_FMT_S32:
        return audio_format::AUDIO_FORMAT_32BIT;
    case AV_SAMPLE_FMT_FLT:
        return audio_format::AUDIO_FORMAT_FLOAT;
    case AV_SAMPLE_FMT_U8P:
        return audio_format::AUDIO_FORMAT_U8BIT_PLANAR;
    case AV_SAMPLE_FMT_S16P:
        return audio_format::AUDIO_FORMAT_16BIT_PLANAR;
    case AV_SAMPLE_FMT_S32P:
        return audio_format::AUDIO_FORMAT_32BIT_PLANAR;
    case AV_SAMPLE_FMT_FLTP:
        return audio_format::AUDIO_FORMAT_FLOAT_PLANAR;
    }

    /* shouldn't get here */
    return audio_format::AUDIO_FORMAT_16BIT;
}

static enum AVChromaLocation
        determine_chroma_location(enum AVPixelFormat pix_fmt,
                                  enum AVColorSpace colorspace)
{
    const AVPixFmtDescriptor *const desc = av_pix_fmt_desc_get(pix_fmt);
    if (desc) {
        const unsigned log_chroma_w = desc->log2_chroma_w;
        const unsigned log_chroma_h = desc->log2_chroma_h;
        switch (log_chroma_h) {
        case 0:
            switch (log_chroma_w) {
            case 0:
                /* 4:4:4 */
                return AVCHROMA_LOC_CENTER;
            case 1:
                /* 4:2:2 */
                return AVCHROMA_LOC_LEFT;
            }
            break;
        case 1:
            if (log_chroma_w == 1) {
                /* 4:2:0 */
                return (colorspace == AVCOL_SPC_BT2020_NCL)
                        ? AVCHROMA_LOC_TOPLEFT
                        : AVCHROMA_LOC_LEFT;
            }
        }
    }

    return AVCHROMA_LOC_UNSPECIFIED;
}
