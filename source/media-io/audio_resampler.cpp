#include "lite-obs/media-io/audio_resampler.h"

extern "C"
{
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#include "lite-obs/media-io/audio_info.h"
#include "lite-obs/util/log.h"

struct audio_resampler_private {
    SwrContext *context{};
    bool opened{};

    uint32_t input_freq{};
    uint64_t input_layout{};
    AVSampleFormat input_format = AV_SAMPLE_FMT_NONE;

    uint8_t *output_buffer[MAX_AV_PLANES]{};
    uint64_t output_layout{};
    AVSampleFormat output_format = AV_SAMPLE_FMT_NONE;
    int output_size{};
    uint32_t output_ch{};
    uint32_t output_freq{};
    uint32_t output_planes{};

    ~audio_resampler_private() {
        if (context)
            swr_free(&context);
        if (output_buffer[0])
            av_freep(&output_buffer[0]);
    }
};


static inline enum AVSampleFormat convert_audio_format(audio_format format)
{
    switch (format) {
    case audio_format::AUDIO_FORMAT_UNKNOWN:
        return AV_SAMPLE_FMT_S16;
    case audio_format::AUDIO_FORMAT_U8BIT:
        return AV_SAMPLE_FMT_U8;
    case audio_format::AUDIO_FORMAT_16BIT:
        return AV_SAMPLE_FMT_S16;
    case audio_format::AUDIO_FORMAT_32BIT:
        return AV_SAMPLE_FMT_S32;
    case audio_format::AUDIO_FORMAT_FLOAT:
        return AV_SAMPLE_FMT_FLT;
    case audio_format::AUDIO_FORMAT_U8BIT_PLANAR:
        return AV_SAMPLE_FMT_U8P;
    case audio_format::AUDIO_FORMAT_16BIT_PLANAR:
        return AV_SAMPLE_FMT_S16P;
    case audio_format::AUDIO_FORMAT_32BIT_PLANAR:
        return AV_SAMPLE_FMT_S32P;
    case audio_format::AUDIO_FORMAT_FLOAT_PLANAR:
        return AV_SAMPLE_FMT_FLTP;
    }

    /* shouldn't get here */
    return AV_SAMPLE_FMT_S16;
}

static inline uint64_t convert_speaker_layout(speaker_layout layout)
{
    switch (layout) {
    case speaker_layout::SPEAKERS_UNKNOWN:
        return 0;
    case speaker_layout::SPEAKERS_MONO:
        return AV_CH_LAYOUT_MONO;
    case speaker_layout::SPEAKERS_STEREO:
        return AV_CH_LAYOUT_STEREO;
    case speaker_layout::SPEAKERS_2POINT1:
        return AV_CH_LAYOUT_SURROUND;
    case speaker_layout::SPEAKERS_4POINT0:
        return AV_CH_LAYOUT_4POINT0;
    case speaker_layout::SPEAKERS_4POINT1:
        return AV_CH_LAYOUT_4POINT1;
    case speaker_layout::SPEAKERS_5POINT1:
        return AV_CH_LAYOUT_5POINT1_BACK;
    case speaker_layout::SPEAKERS_7POINT1:
        return AV_CH_LAYOUT_7POINT1;
    }

    /* shouldn't get here */
    return 0;
}

audio_resampler::audio_resampler()
{
    d_ptr = std::make_unique<audio_resampler_private>();
}

audio_resampler::~audio_resampler()
{

}

bool audio_resampler::create(const resample_info *dst, const resample_info *src)
{
    d_ptr->opened = false;
    d_ptr->input_freq = src->samples_per_sec;
    d_ptr->input_layout = convert_speaker_layout(src->speakers);
    d_ptr->input_format = convert_audio_format(src->format);
    d_ptr->output_size = 0;
    d_ptr->output_ch = get_audio_channels(dst->speakers);
    d_ptr->output_freq = dst->samples_per_sec;
    d_ptr->output_layout = convert_speaker_layout(dst->speakers);
    d_ptr->output_format = convert_audio_format(dst->format);
    d_ptr->output_planes = is_audio_planar(dst->format) ? d_ptr->output_ch : 1;

    d_ptr->context = swr_alloc_set_opts(NULL, d_ptr->output_layout,
                                        d_ptr->output_format,
                                        dst->samples_per_sec, d_ptr->input_layout,
                                        d_ptr->input_format, src->samples_per_sec,
                                        0, NULL);

    if (!d_ptr->context) {
        return false;
    }

    if (d_ptr->input_layout == AV_CH_LAYOUT_MONO && d_ptr->output_ch > 1) {
        const double matrix[MAX_AUDIO_CHANNELS][MAX_AUDIO_CHANNELS] = {
            {1},
            {1, 1},
            {1, 1, 0},
            {1, 1, 1, 1},
            {1, 1, 1, 0, 1},
            {1, 1, 1, 1, 1, 1},
            {1, 1, 1, 0, 1, 1, 1},
            {1, 1, 1, 0, 1, 1, 1, 1},
        };
        if (swr_set_matrix(d_ptr->context, matrix[d_ptr->output_ch - 1], 1) <
                0)
            blog(LOG_DEBUG,
                 "swr_set_matrix failed for mono upmix\n");
    }

    auto errcode = swr_init(d_ptr->context);
    if (errcode != 0) {
        blog(LOG_ERROR, "avresample_open failed: error code %d",
             errcode);
        return false;
    }

    return true;
}

bool audio_resampler::do_resample(uint8_t *output[], uint32_t *out_frames,
                                  uint64_t *ts_offset,
                                  const uint8_t *const input[],
                                  uint32_t in_frames)
{
    if (!d_ptr)
        return false;

    struct SwrContext *context = d_ptr->context;
    int ret;

    int64_t delay = swr_get_delay(context, d_ptr->input_freq);
    int estimated = (int)av_rescale_rnd(delay + (int64_t)in_frames,
                                        (int64_t)d_ptr->output_freq,
                                        (int64_t)d_ptr->input_freq,
                                        AV_ROUND_UP);

    *ts_offset = (uint64_t)swr_get_delay(context, 1000000000);

    /* resize the buffer if bigger */
    if (estimated > d_ptr->output_size) {
        if (d_ptr->output_buffer[0])
            av_freep(&d_ptr->output_buffer[0]);

        av_samples_alloc(d_ptr->output_buffer, NULL, d_ptr->output_ch,
                         estimated, d_ptr->output_format, 0);

        d_ptr->output_size = estimated;
    }

    ret = swr_convert(context, d_ptr->output_buffer, d_ptr->output_size,
                      (const uint8_t **)input, in_frames);

    if (ret < 0) {
        blog(LOG_ERROR, "swr_convert failed: %d", ret);
        return false;
    }

    for (uint32_t i = 0; i < d_ptr->output_planes; i++)
        output[i] = d_ptr->output_buffer[i];

    *out_frames = (uint32_t)ret;
    return true;

}
