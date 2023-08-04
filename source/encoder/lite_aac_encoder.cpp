#include "lite-obs/encoder/lite_aac_encoder.h"
#include "lite-obs/util/log.h"
#include "lite-obs/media-io/audio_output.h"
#include "lite-obs/media-io/ffmpeg_formats.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

#include <vector>

#if LIBAVCODEC_VERSION_MAJOR >= 58
#define CODEC_CAP_TRUNC AV_CODEC_CAP_TRUNCATED
#define CODEC_FLAG_TRUNC AV_CODEC_FLAG_TRUNCATED
#define CODEC_FLAG_GLOBAL_H AV_CODEC_FLAG_GLOBAL_HEADER
#else
#define CODEC_CAP_TRUNC CODEC_CAP_TRUNCATED
#define CODEC_FLAG_TRUNC CODEC_FLAG_TRUNCATED
#define CODEC_FLAG_GLOBAL_H CODEC_FLAG_GLOBAL_HEADER
#endif

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

static inline speaker_layout
convert_ff_channel_layout(uint64_t channel_layout)
{
    switch (channel_layout) {
    case AV_CH_LAYOUT_MONO:
        return speaker_layout::SPEAKERS_MONO;
    case AV_CH_LAYOUT_STEREO:
        return speaker_layout::SPEAKERS_STEREO;
    case AV_CH_LAYOUT_SURROUND:
        return speaker_layout::SPEAKERS_2POINT1;
    case AV_CH_LAYOUT_4POINT0:
        return speaker_layout::SPEAKERS_4POINT0;
    case AV_CH_LAYOUT_4POINT1:
        return speaker_layout::SPEAKERS_4POINT1;
    case AV_CH_LAYOUT_5POINT1_BACK:
        return speaker_layout::SPEAKERS_5POINT1;
    case AV_CH_LAYOUT_7POINT1:
        return speaker_layout::SPEAKERS_7POINT1;
    }

    /* shouldn't get here */
    return speaker_layout::SPEAKERS_UNKNOWN;
}

struct lite_aac_encoder_private
{
    const char *type{};

    bool initilized{};

    const AVCodec *codec{};
    AVCodecContext *context{};

    uint8_t *samples[MAX_AV_PLANES]{};
    AVFrame *aframe{};
    int64_t total_samples{};

    std::shared_ptr<std::vector<uint8_t>> packet_buffer{};

    size_t audio_planes{};
    size_t audio_size{};

    int frame_size{}; /* pretty much always 1024 for AAC */
    int frame_size_bytes{};
};

lite_aac_encoder::lite_aac_encoder(int bitrate, size_t mixer_idx)
    : lite_obs_encoder(bitrate, mixer_idx)
{
    d_ptr = std::make_unique<lite_aac_encoder_private>();
    d_ptr->packet_buffer = std::make_shared<std::vector<uint8_t>>();
}

lite_aac_encoder::~lite_aac_encoder()
{

}

const char *lite_aac_encoder::i_encoder_codec()
{
    return "AAC";
}

obs_encoder_type lite_aac_encoder::i_encoder_type()
{
    return obs_encoder_type::OBS_ENCODER_AUDIO;
}

void lite_aac_encoder::init_sizes(std::shared_ptr<audio_output> audio)
{
    auto aoi = audio->audio_output_get_info();
    auto format = convert_ffmpeg_sample_format(d_ptr->context->sample_fmt);

    d_ptr->audio_planes = get_audio_planes(format, aoi->speakers);
    d_ptr->audio_size = get_audio_size(format, aoi->speakers, 1);
}

bool lite_aac_encoder::initialize_codec()
{
    int ret;

    d_ptr->aframe = av_frame_alloc();
    if (!d_ptr->aframe) {
        blog(LOG_WARNING, "Failed to allocate audio frame");
        return false;
    }

    ret = avcodec_open2(d_ptr->context, d_ptr->codec, NULL);
    if (ret < 0) {
        blog(LOG_WARNING, "Failed to open AAC codec: %d", ret);
        return false;
    }
    d_ptr->aframe->format = d_ptr->context->sample_fmt;
    d_ptr->aframe->channels = d_ptr->context->channels;
    d_ptr->aframe->channel_layout = d_ptr->context->channel_layout;
    d_ptr->aframe->sample_rate = d_ptr->context->sample_rate;

    d_ptr->frame_size = d_ptr->context->frame_size;
    if (!d_ptr->frame_size)
        d_ptr->frame_size = 1024;

    d_ptr->frame_size_bytes = d_ptr->frame_size * (int)d_ptr->audio_size;

    ret = av_samples_alloc(d_ptr->samples, NULL, d_ptr->context->channels,
                           d_ptr->frame_size, d_ptr->context->sample_fmt, 0);
    if (ret < 0) {
        blog(LOG_WARNING, "Failed to create audio buffer: %d", ret);
        return false;
    }

    return true;
}

bool lite_aac_encoder::i_create()
{
    int bitrate = lite_obs_encoder_bitrate();
    auto audio = lite_obs_encoder_audio();

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    avcodec_register_all();
#endif

    d_ptr->codec = avcodec_find_encoder_by_name("aac");
    d_ptr->type = "aac";

    blog(LOG_INFO, "---------------------------------");

    do {
        if (!d_ptr->codec) {
            blog(LOG_WARNING, "Couldn't find encoder");
            break;
        }

        if (!bitrate) {
            blog(LOG_WARNING, "Invalid bitrate specified");
            return false;
        }

        d_ptr->context = avcodec_alloc_context3(d_ptr->codec);
        if (!d_ptr->context) {
            blog(LOG_WARNING, "Failed to create codec context");
            break;
        }

        d_ptr->context->bit_rate = bitrate * 1000;
        auto aoi = audio->audio_output_get_info();
        d_ptr->context->channels = (int)audio->audio_output_get_channels();
        d_ptr->context->channel_layout = convert_speaker_layout(aoi->speakers);
        d_ptr->context->sample_rate = audio->audio_output_get_sample_rate();
        d_ptr->context->sample_fmt = d_ptr->codec->sample_fmts ? d_ptr->codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;

        /* check to make sure sample rate is supported */
        if (d_ptr->codec->supported_samplerates) {
            const int *rate = d_ptr->codec->supported_samplerates;
            int cur_rate = d_ptr->context->sample_rate;
            int closest = 0;

            while (*rate) {
                int dist = abs(cur_rate - *rate);
                int closest_dist = abs(cur_rate - closest);

                if (dist < closest_dist)
                    closest = *rate;
                rate++;
            }

            if (closest)
                d_ptr->context->sample_rate = closest;
        }

        if (strcmp(d_ptr->codec->name, "aac") == 0) {
            av_opt_set(d_ptr->context->priv_data, "aac_coder", "fast", 0);
        }

        blog(LOG_INFO, "bitrate: %" PRId64 ", channels: %d, channel_layout: %x\n",
             (int64_t)d_ptr->context->bit_rate / 1000,
             (int)d_ptr->context->channels,
             (unsigned int)d_ptr->context->channel_layout);

        init_sizes(audio);

        /* enable experimental FFmpeg encoder if the only one available */
        d_ptr->context->strict_std_compliance = -2;

        d_ptr->context->flags = CODEC_FLAG_GLOBAL_H;

        if (initialize_codec()) {
            d_ptr->initilized = true;
            return true;
        }

    } while (0);

    i_destroy();
    return false;
}

void lite_aac_encoder::i_destroy()
{
    if (d_ptr->samples[0])
        av_freep(&d_ptr->samples[0]);
    if (d_ptr->context)
        avcodec_close(d_ptr->context);
    if (d_ptr->aframe)
        av_frame_free(&d_ptr->aframe);

    d_ptr->packet_buffer.reset();

    d_ptr->initilized = false;
}

bool lite_aac_encoder::i_encoder_valid()
{
    return d_ptr->initilized;
}

bool lite_aac_encoder::i_encode_internal(std::shared_ptr<encoder_packet> packet, bool *received_packet)
{
    AVRational time_base = {1, d_ptr->context->sample_rate};
    AVPacket avpacket{};

    d_ptr->aframe->nb_samples = d_ptr->frame_size;
    d_ptr->aframe->pts = av_rescale_q(
                d_ptr->total_samples, {1, d_ptr->context->sample_rate},
                d_ptr->context->time_base);

    auto ret = avcodec_fill_audio_frame(
                d_ptr->aframe, d_ptr->context->channels, d_ptr->context->sample_fmt,
                d_ptr->samples[0], d_ptr->frame_size_bytes * d_ptr->context->channels, 1);
    if (ret < 0) {
        blog(LOG_WARNING, "avcodec_fill_audio_frame failed: %d", ret);
        return false;
    }

    d_ptr->total_samples += d_ptr->frame_size;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 40, 101)
    ret = avcodec_send_frame(d_ptr->context, d_ptr->aframe);
    if (ret == 0)
        ret = avcodec_receive_packet(d_ptr->context, &avpacket);

    auto got_packet = (ret == 0);

    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
        ret = 0;
#else
    ret = avcodec_encode_audio2(d_ptr->context, &avpacket, d_ptr->aframe,
                                &got_packet);
#endif
    if (ret < 0) {
        blog(LOG_WARNING, "avcodec_encode_audio2 failed: %s", ret);
        return false;
    }

    *received_packet = !!got_packet;
    if (!got_packet)
        return true;

    d_ptr->packet_buffer->resize(avpacket.size);
    memcpy(d_ptr->packet_buffer->data(), avpacket.data, avpacket.size);

    packet->pts = rescale_ts(avpacket.pts, d_ptr->context, time_base);
    packet->dts = rescale_ts(avpacket.dts, d_ptr->context, time_base);
    packet->data = d_ptr->packet_buffer;
    packet->type = obs_encoder_type::OBS_ENCODER_AUDIO;
    packet->timebase_num = 1;
    packet->timebase_den = (int32_t)d_ptr->context->sample_rate;
    av_packet_unref(&avpacket);
    return true;
}

bool lite_aac_encoder::i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void(std::shared_ptr<encoder_packet>)> send_off)
{
    for (size_t i = 0; i < d_ptr->audio_planes; i++)
        memcpy(d_ptr->samples[i], frame->data[i], d_ptr->frame_size_bytes);

    AVRational time_base = {1, d_ptr->context->sample_rate};
    AVPacket avpacket{};

    d_ptr->aframe->nb_samples = d_ptr->frame_size;
    d_ptr->aframe->pts = av_rescale_q(
                d_ptr->total_samples, {1, d_ptr->context->sample_rate},
                d_ptr->context->time_base);

    auto ret = avcodec_fill_audio_frame(
                d_ptr->aframe, d_ptr->context->channels, d_ptr->context->sample_fmt,
                d_ptr->samples[0], d_ptr->frame_size_bytes * d_ptr->context->channels, 1);
    if (ret < 0) {
        blog(LOG_WARNING, "avcodec_fill_audio_frame failed: %d", ret);
        return false;
    }

    d_ptr->total_samples += d_ptr->frame_size;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 40, 101)
    ret = avcodec_send_frame(d_ptr->context, d_ptr->aframe);
    if (ret == 0)
        ret = avcodec_receive_packet(d_ptr->context, &avpacket);

    auto got_packet = (ret == 0);

    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
        ret = 0;
#else
    ret = avcodec_encode_audio2(d_ptr->context, &avpacket, d_ptr->aframe,
                                &got_packet);
#endif
    if (ret < 0) {
        blog(LOG_WARNING, "avcodec_encode_audio2 failed: %s", ret);
        return false;
    }

    if (!got_packet)
        return true;

    d_ptr->packet_buffer->resize(avpacket.size);
    memcpy(d_ptr->packet_buffer->data(), avpacket.data, avpacket.size);

    packet->pts = rescale_ts(avpacket.pts, d_ptr->context, time_base);
    packet->dts = rescale_ts(avpacket.dts, d_ptr->context, time_base);
    packet->data = d_ptr->packet_buffer;
    packet->type = obs_encoder_type::OBS_ENCODER_AUDIO;
    packet->timebase_num = 1;
    packet->timebase_den = (int32_t)d_ptr->context->sample_rate;
    send_off(packet);

    av_packet_unref(&avpacket);
    return true;
}

size_t lite_aac_encoder::i_get_frame_size()
{
    return d_ptr->frame_size;
}

bool lite_aac_encoder::i_get_extra_data(uint8_t **extra_data, size_t *size)
{
    *extra_data = d_ptr->context->extradata;
    *size = d_ptr->context->extradata_size;
    return true;
}

void lite_aac_encoder::i_get_audio_info(audio_convert_info *info)
{
    info->format = convert_ffmpeg_sample_format(d_ptr->context->sample_fmt);
    info->samples_per_sec = (uint32_t)d_ptr->context->sample_rate;
    info->speakers =
            convert_ff_channel_layout(d_ptr->context->channel_layout);
}

