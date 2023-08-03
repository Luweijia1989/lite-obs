#include "lite-obs/media-io/video_scaler.h"
#include "lite-obs/util/log.h"

extern "C" {
#include <libswscale/swscale.h>
}

struct video_scaler_private {
    struct SwsContext *swscale{};
    int src_height{};

    ~video_scaler_private() {
        if (swscale)
            sws_freeContext(swscale);
    }
};

static inline enum AVPixelFormat
        get_ffmpeg_video_format(video_format format)
{
    switch (format) {
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
    case video_format::VIDEO_FORMAT_I444:
        return AV_PIX_FMT_YUV444P;
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

static inline int get_ffmpeg_scale_type(video_scale_type type)
{
    switch (type) {
    case video_scale_type::VIDEO_SCALE_DEFAULT:
        return SWS_FAST_BILINEAR;
    case video_scale_type::VIDEO_SCALE_POINT:
        return SWS_POINT;
    case video_scale_type::VIDEO_SCALE_FAST_BILINEAR:
        return SWS_FAST_BILINEAR;
    case video_scale_type::VIDEO_SCALE_BILINEAR:
        return SWS_BILINEAR | SWS_AREA;
    case video_scale_type::VIDEO_SCALE_BICUBIC:
        return SWS_BICUBIC;
    }

    return SWS_POINT;
}

static inline const int *get_ffmpeg_coeffs(video_colorspace cs)
{
    switch (cs) {
    case video_colorspace::VIDEO_CS_DEFAULT:
        return sws_getCoefficients(SWS_CS_ITU601);
    case video_colorspace::VIDEO_CS_601:
        return sws_getCoefficients(SWS_CS_ITU601);
    case video_colorspace::VIDEO_CS_709:
        return sws_getCoefficients(SWS_CS_ITU709);
    }

    return sws_getCoefficients(SWS_CS_ITU601);
}

static inline int get_ffmpeg_range_type(video_range_type type)
{
    switch (type) {
    case video_range_type::VIDEO_RANGE_DEFAULT:
        return 0;
    case video_range_type::VIDEO_RANGE_PARTIAL:
        return 0;
    case video_range_type::VIDEO_RANGE_FULL:
        return 1;
    }

    return 0;
}

#define FIXED_1_0 (1 << 16)

video_scaler::video_scaler()
{
    d_ptr = std::make_unique<video_scaler_private>();
}

video_scaler::~video_scaler()
{

}

int video_scaler::create(const video_scale_info *dst, const video_scale_info *src, video_scale_type type)
{
    AVPixelFormat format_src = get_ffmpeg_video_format(src->format);
    AVPixelFormat format_dst = get_ffmpeg_video_format(dst->format);
    int scale_type = get_ffmpeg_scale_type(type);
    const int *coeff_src = get_ffmpeg_coeffs(src->colorspace);
    const int *coeff_dst = get_ffmpeg_coeffs(dst->colorspace);
    int range_src = get_ffmpeg_range_type(src->range);
    int range_dst = get_ffmpeg_range_type(dst->range);

    if (format_src == AV_PIX_FMT_NONE || format_dst == AV_PIX_FMT_NONE)
        return VIDEO_SCALER_BAD_CONVERSION;

    d_ptr->src_height = src->height;
    d_ptr->swscale = sws_getCachedContext(NULL, src->width, src->height,
                                          format_src, dst->width,
                                          dst->height, format_dst,
                                          scale_type, NULL, NULL, NULL);
    if (!d_ptr->swscale) {
        blog(LOG_ERROR, "video_scaler_create: Could not create "
                        "swscale");
        return VIDEO_SCALER_FAILED;
    }

    auto ret = sws_setColorspaceDetails(d_ptr->swscale, coeff_src, range_src,
                                        coeff_dst, range_dst, 0, FIXED_1_0,
                                        FIXED_1_0);
    if (ret < 0) {
        blog(LOG_DEBUG, "video_scaler_create: "
                        "sws_setColorspaceDetails failed, ignoring");
    }

    return VIDEO_SCALER_SUCCESS;
}

bool video_scaler::video_scaler_scale(uint8_t *output[], uint32_t out_linesize[], const uint8_t * const input[], const uint32_t in_linesize[])
{
    if (!d_ptr->swscale)
        return false;

    int ret = sws_scale(d_ptr->swscale, input, (const int *)in_linesize, 0,
                        d_ptr->src_height, output,
                        (const int *)out_linesize);
    if (ret <= 0) {
        blog(LOG_ERROR, "video_scaler_scale: sws_scale failed: %d",
             ret);
        return false;
    }

    return true;
}
