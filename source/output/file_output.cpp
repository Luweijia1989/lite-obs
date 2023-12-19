#include "lite-obs/output/file_output.h"
#include "lite-obs/util/log.h"
#include <atomic>
#include <list>
#include <thread>
#include "lite-obs/lite_encoder.h"
#include "lite-obs/media-io/ffmpeg_formats.h"
#include "lite-obs/media-io/video_output.h"
#include "lite-obs/media-io/audio_output.h"

#if LIBAVCODEC_VERSION_MAJOR >= 58
#define CODEC_FLAG_GLOBAL_H AV_CODEC_FLAG_GLOBAL_HEADER
#else
#define CODEC_FLAG_GLOBAL_H CODEC_FLAG_GLOBAL_HEADER
#endif

struct lite_ffmpeg_mux_private
{
    struct main_params {
        int has_video{};
        int tracks{};
        int vbitrate{};
        int gop{};
        int width{};
        int height{};
        int fps_num{};
        int fps_den{};
        AVColorPrimaries color_primaries{};
        AVColorTransferCharacteristic color_trc{};
        AVColorSpace colorspace{};
        AVColorRange color_range{};
        AVChromaLocation chroma_sample_location{};
        const char *vcodec = "h264";
        const char *acodec = "aac";
    };

    struct audio_params {
        int abitrate{};
        int sample_rate{};
        int channels{};
        int frame_size{};
    };

    struct audio_info {
        AVStream *stream{};
        AVCodecContext *ctx{};
    };

    std::string output_path{};
    int64_t stop_ts{};
    uint64_t total_bytes{};
    std::string path{};
    bool mux_inited{};
    std::atomic_bool active{};
    std::atomic_bool stopping{};
    std::atomic_bool capturing{};
    bool initilized{};

    AVFormatContext *output{};
    AVStream *video_stream{};
    AVCodecContext *video_ctx{};
    AVPacket *packet{};
    audio_info audio_infos{};
    main_params params{};
    audio_params audio{};
    int num_audio_streams{};
    char error[4096]{};

    lite_ffmpeg_mux_private() {

    }
};

lite_ffmpeg_mux::lite_ffmpeg_mux()
    : lite_obs_output()
{
    d_ptr = std::make_unique<lite_ffmpeg_mux_private>();
}

lite_ffmpeg_mux::~lite_ffmpeg_mux()
{

}

void lite_ffmpeg_mux::i_set_output_info(void *info)
{
    d_ptr->output_path = (char *)info;
}

bool lite_ffmpeg_mux::i_output_valid()
{
    return d_ptr->initilized;
}

bool lite_ffmpeg_mux::i_has_video()
{
    return true;
}

bool lite_ffmpeg_mux::i_has_audio()
{
    return true;
}

bool lite_ffmpeg_mux::i_encoded()
{
    return true;
}

bool lite_ffmpeg_mux::i_create()
{
    d_ptr->initilized = true;
    return true;
}

void lite_ffmpeg_mux::i_destroy()
{
    if (d_ptr->initilized) {
        if (d_ptr->output)
            av_write_trailer(d_ptr->output);
    }

    free_avformat();

    av_packet_free(&d_ptr->packet);

    d_ptr->initilized = false;
    d_ptr.reset();
}

bool lite_ffmpeg_mux::i_start()
{
    if (!lite_obs_output_can_begin_data_capture())
        return false;
    if (!lite_obs_output_initialize_encoders())
        return false;

    /* write headers and start capture */
    d_ptr->active = true;
    d_ptr->capturing = true;
    d_ptr->total_bytes = 0;
    lite_obs_output_begin_data_capture();

    blog(LOG_INFO, "Writing file '%s'...", d_ptr->output_path.c_str());
    return true;
}

void lite_ffmpeg_mux::i_stop(uint64_t ts)
{
    if (d_ptr->capturing || ts == 0) {
        d_ptr->stop_ts = (int64_t)ts / 1000LL;
        d_ptr->stopping = true;
        d_ptr->capturing =false;
    }
}

void lite_ffmpeg_mux::i_raw_video(struct video_data *frame)
{

}

void lite_ffmpeg_mux::i_raw_audio(struct audio_data *frames)
{

}

void lite_ffmpeg_mux::i_encoded_packet(std::shared_ptr<struct encoder_packet> packet)
{
    if (!d_ptr->active)
        return;

    if (!d_ptr->mux_inited) {
        d_ptr->mux_inited = true;

        init_params();
        if (!mux_init()) {
            deactivate(0);
            lite_obs_output_signal_stop(LITE_OBS_OUTPUT_ERROR);
            d_ptr->capturing = false;
            return;
        }
    }

    if (d_ptr->stopping) {
        if (packet->sys_dts_usec >= d_ptr->stop_ts) {
            deactivate(0);
            return;
        }
    }

    int idx = -1;
    auto is_audio = packet->type == obs_encoder_type::OBS_ENCODER_AUDIO;
    if (is_audio)
        idx = d_ptr->audio_infos.stream->id;
    else
        idx = d_ptr->video_stream->id;

    if (idx == -1) {
        return;
    }

    auto rescale_ts = [this](AVRational codec_time_base, int64_t val, int idx)
    {
        AVStream *stream = d_ptr->output->streams[idx];

        return av_rescale_q_rnd(val / codec_time_base.num, codec_time_base, stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    };

    const AVRational codec_time_base = is_audio ? d_ptr->audio_infos.ctx->time_base : d_ptr->video_ctx->time_base;

    d_ptr->packet->data = packet->data->data();
    d_ptr->packet->size = (int)packet->data->size();
    d_ptr->packet->stream_index = idx;
    d_ptr->packet->pts = rescale_ts(codec_time_base, packet->pts, idx);
    d_ptr->packet->dts = rescale_ts(codec_time_base, packet->dts, idx);

    if (packet->keyframe)
        d_ptr->packet->flags = AV_PKT_FLAG_KEY;

    av_interleaved_write_frame(d_ptr->output, d_ptr->packet);

    d_ptr->total_bytes += packet->data->size();
}

uint64_t lite_ffmpeg_mux::i_get_total_bytes()
{
    return 0;
}

int lite_ffmpeg_mux::i_get_dropped_frames()
{
    return 0;
}

void lite_ffmpeg_mux::init_params()
{
    auto video_encoder = lite_obs_output_get_video_encoder();
    auto audio_encoder = lite_obs_output_get_audio_encoder(0);

    auto output_info = lite_obs_output_video()->video_output_get_info();

    d_ptr->params.has_video = video_encoder != nullptr;
    d_ptr->params.tracks = 1;
    d_ptr->params.vbitrate = video_encoder->lite_obs_encoder_bitrate();
    d_ptr->params.width = lite_obs_output_get_width();
    d_ptr->params.height = lite_obs_output_get_height();
    d_ptr->params.fps_num = output_info->fps_num;
    d_ptr->params.fps_den = output_info->fps_den;

    enum AVColorPrimaries pri = AVCOL_PRI_UNSPECIFIED;
    enum AVColorTransferCharacteristic trc = AVCOL_TRC_UNSPECIFIED;
    enum AVColorSpace spc = AVCOL_SPC_UNSPECIFIED;
    switch (output_info->colorspace) {
    case video_colorspace::VIDEO_CS_601:
        pri = AVCOL_PRI_SMPTE170M;
        trc = AVCOL_TRC_SMPTE170M;
        spc = AVCOL_SPC_SMPTE170M;
        break;
    case video_colorspace::VIDEO_CS_DEFAULT:
    case video_colorspace::VIDEO_CS_709:
        pri = AVCOL_PRI_BT709;
        trc = AVCOL_TRC_BT709;
        spc = AVCOL_SPC_BT709;
        break;
    }
    d_ptr->params.color_primaries = pri;
    d_ptr->params.color_trc = trc;
    d_ptr->params.colorspace = spc;

    const enum AVColorRange range = (output_info->range == video_range_type::VIDEO_RANGE_FULL)
                                        ? AVCOL_RANGE_JPEG
                                        : AVCOL_RANGE_MPEG;
    d_ptr->params.color_range = range;
    d_ptr->params.chroma_sample_location = determine_chroma_location(lite_obs_to_ffmpeg_video_format(output_info->format), spc);

    d_ptr->audio.abitrate = audio_encoder->lite_obs_encoder_bitrate();
    d_ptr->audio.sample_rate = audio_encoder->lite_obs_encoder_get_sample_rate();
    d_ptr->audio.channels = (int)lite_obs_output_audio()->audio_output_get_channels();
    d_ptr->audio.frame_size = (int)audio_encoder->lite_obs_encoder_get_frame_size();
}

bool lite_ffmpeg_mux::new_stream(AVStream **stream, const char *name)
{
    *stream = avformat_new_stream(d_ptr->output, NULL);
    if (!*stream) {
        blog(LOG_ERROR, "Couldn't create stream for encoder '%s'\n", name);
        return false;
    }

    (*stream)->id = d_ptr->output->nb_streams - 1;
    return true;
}

void lite_ffmpeg_mux::create_video_stream()
{
    auto video_encoder = lite_obs_output_get_video_encoder();
    if (!video_encoder)
        return;

    const char *name = d_ptr->params.vcodec;
    const AVCodecDescriptor *codec = avcodec_descriptor_get_by_name(name);
    if (!codec) {
        fprintf(stderr, "Couldn't find codec '%s'\n", name);
        return;
    }

    if (!new_stream(&d_ptr->video_stream, name))
        return;

    void *extradata = NULL;
    uint8_t *extra_data = nullptr;
    size_t extra_data_size = 0;
    video_encoder->lite_obs_encoder_get_extra_data(&extra_data, &extra_data_size);
    if (extra_data_size) {
        extradata = av_memdup(extra_data, extra_data_size);
    }

    auto context = avcodec_alloc_context3(NULL);
    context->codec_type = codec->type;
    context->codec_id = codec->id;
    context->bit_rate = (int64_t)d_ptr->params.vbitrate * 1000;
    context->width = d_ptr->params.width;
    context->height = d_ptr->params.height;
    context->coded_width = d_ptr->params.width;
    context->coded_height = d_ptr->params.height;
    context->color_primaries = d_ptr->params.color_primaries;
    context->color_trc = d_ptr->params.color_trc;
    context->colorspace = d_ptr->params.colorspace;
    context->color_range = d_ptr->params.color_range;
    context->chroma_sample_location = d_ptr->params.chroma_sample_location;
    context->extradata = (uint8_t *)extradata;
    context->extradata_size = (int)extra_data_size;
    context->time_base = {d_ptr->params.fps_den, d_ptr->params.fps_num};

    d_ptr->video_stream->time_base = context->time_base;
#if LIBAVFORMAT_VERSION_MAJOR < 59
    // codec->time_base may still be used if LIBAVFORMAT_VERSION_MAJOR < 59
#pragma warning(push)
#pragma deprecated
    d_ptr->video_stream->codec->time_base = context->time_base;
#pragma warning(pop)
#endif

    d_ptr->video_stream->avg_frame_rate = av_inv_q(context->time_base);

    if (d_ptr->output->oformat->flags & AVFMT_GLOBALHEADER)
        context->flags |= CODEC_FLAG_GLOBAL_H;

    avcodec_parameters_from_context(d_ptr->video_stream->codecpar, context);

    d_ptr->video_ctx = context;
}

void lite_ffmpeg_mux::create_audio_stream()
{
    auto audio_encoder = lite_obs_output_get_audio_encoder(0);
    if (!audio_encoder)
        return;

    const char *name = d_ptr->params.acodec;
    const AVCodecDescriptor *codec = avcodec_descriptor_get_by_name(name);
    if (!codec) {
        fprintf(stderr, "Couldn't find codec '%s'\n", name);
        return;
    }

    AVStream *stream = nullptr;
    if (!new_stream(&stream, name))
        return;

    stream->time_base = {1, d_ptr->audio.sample_rate};

    void *extradata = NULL;
    uint8_t *extra_data = nullptr;
    size_t extra_data_size = 0;
    audio_encoder->lite_obs_encoder_get_extra_data(&extra_data, &extra_data_size);
    if (extra_data_size) {
        extradata = av_memdup(extra_data, extra_data_size);
    }

    auto context = avcodec_alloc_context3(NULL);
    context->codec_type = codec->type;
    context->codec_id = codec->id;
    context->bit_rate = (int64_t)d_ptr->audio.abitrate * 1000;
    auto channels = d_ptr->audio.channels;
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 24, 100)
    context->channels = channels;
#endif
    context->sample_rate = d_ptr->audio.sample_rate;
    context->frame_size = d_ptr->audio.frame_size;
    context->sample_fmt = AV_SAMPLE_FMT_S16;
    context->time_base = stream->time_base;
    context->extradata = (uint8_t *)extradata;
    context->extradata_size = (int)extra_data_size;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 24, 100)
    context->channel_layout = av_get_default_channel_layout(channels);
    //avutil default channel layout for 5 channels is 5.0 ; fix for 4.1
    if (channels == 5)
        context->channel_layout = av_get_channel_layout("4.1");
#else
    av_channel_layout_default(&context->ch_layout, channels);
    //avutil default channel layout for 5 channels is 5.0 ; fix for 4.1
    if (channels == 5)
        context->ch_layout = AV_CHANNEL_LAYOUT_4POINT1;
#endif
    if (d_ptr->output->oformat->flags & AVFMT_GLOBALHEADER)
        context->flags |= CODEC_FLAG_GLOBAL_H;

    avcodec_parameters_from_context(stream->codecpar, context);

    d_ptr->audio_infos.stream = stream;
    d_ptr->audio_infos.ctx = context;
    d_ptr->num_audio_streams++;
}

bool lite_ffmpeg_mux::init_streams()
{
    create_video_stream();
    create_audio_stream();

    if (!d_ptr->video_stream && !d_ptr->num_audio_streams)
        return false;

    return true;
}

void lite_ffmpeg_mux::free_avformat()
{
    if (d_ptr->output) {
        avcodec_free_context(&d_ptr->video_ctx);

        if ((d_ptr->output->oformat->flags & AVFMT_NOFILE) == 0)
            avio_close(d_ptr->output->pb);

        avformat_free_context(d_ptr->output);
        d_ptr->output = nullptr;
    }

    avcodec_free_context(&d_ptr->audio_infos.ctx);

    d_ptr->audio_infos.ctx = nullptr;
    d_ptr->video_stream = nullptr;
    d_ptr->num_audio_streams = 0;
}

bool lite_ffmpeg_mux::open_output_file()
{
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 0, 100)
    AVOutputFormat *format = d_ptr->output->oformat;
#else
    const AVOutputFormat *format = d_ptr->output->oformat;
#endif

    int ret;

    if ((format->flags & AVFMT_NOFILE) == 0) {
        ret = avio_open(&d_ptr->output->pb, d_ptr->output_path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            blog(LOG_INFO, "Couldn't open '%s', ret: %d", d_ptr->output_path.c_str(), ret);
            return false;
        }
    }

    ret = avformat_write_header(d_ptr->output, NULL);
    if (ret < 0) {
        blog(LOG_INFO, "Error opening '%s', ret: %d", d_ptr->output_path.c_str(), ret);
        return false;
    }

    return true;
}

bool lite_ffmpeg_mux::mux_init()
{
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
#endif

    d_ptr->packet = av_packet_alloc();

    auto output_format = av_guess_format(NULL, d_ptr->output_path.c_str(), NULL);
    if (output_format == NULL) {
        blog(LOG_INFO, "Couldn't find an appropriate muxer for '%s'\n", d_ptr->output_path.c_str());
        return false;
    }

    auto ret = avformat_alloc_output_context2(&d_ptr->output, output_format, NULL, NULL);
    if (ret < 0) {
        blog(LOG_DEBUG, "Couldn't initialize output context: %d\n", ret);
        return false;
    }

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 0, 100)
    d_ptr->output->oformat->video_codec = AV_CODEC_ID_NONE;
    d_ptr->output->oformat->audio_codec = AV_CODEC_ID_NONE;
#endif

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(60, 0, 100)
    /* Allow FLAC/OPUS in MP4 */
    d_ptr->output->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
#endif

    if (!init_streams()) {
        free_avformat();
        return false;
    }

    if (!open_output_file()) {
        free_avformat();
        return false;
    }

    return true;
}

void lite_ffmpeg_mux::deactivate(int code)
{
    if (d_ptr->active) {
        d_ptr->active = false;
        d_ptr->mux_inited = false;
        blog(LOG_INFO, "Output of file '%s' stopped", d_ptr->output_path.c_str());
    }

    if (code) {
        lite_obs_output_signal_stop(code);
    } else if (d_ptr->stopping) {
        lite_obs_output_end_data_capture();
    }

    d_ptr->stopping = false;
}
