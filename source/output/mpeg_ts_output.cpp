#include "lite-obs/output/mpeg_ts_output.h"
#include <thread>
#include <mutex>
#include <list>
#include "lite-obs/util/log.h"
#include "lite-obs/util/circlebuf.h"
#include "lite-obs/util/threading.h"
#include "lite-obs/lite_encoder.h"
#include "lite-obs/media-io/ffmpeg-formats.h"
#include "lite-obs/obs-defs.h"
#include "lite-obs/output/ffmpeg-url.h"
#include "lite-obs/output/ffmpeg-srt.h"
#include "lite-obs/lite_encoder_info.h"

static bool is_srt(const char *url)
{
    return !strncmp(url, SRT_PROTO, sizeof(SRT_PROTO) - 1);
}

static bool proto_is_allowed(const char *url)
{
    return !strncmp(url, UDP_PROTO, sizeof(UDP_PROTO) - 1) ||
            !strncmp(url, TCP_PROTO, sizeof(TCP_PROTO) - 1) ||
            !strncmp(url, HTTP_PROTO, sizeof(HTTP_PROTO) - 1);
}

struct ffmpeg_cfg {
    const char *url{};
    const char *format_name{};
    const char *format_mime_type{};
    const char *muxer_settings{};
    const char *protocol_settings{}; // not used yet for SRT nor RIST
    int gop_size{};
    int video_bitrate{};
    int audio_bitrate{};
    const char *video_encoder{};
    int video_encoder_id{};
    const char *audio_encoder{};
    int audio_encoder_id{};
    const char *video_settings{};
    const char *audio_settings{};
    int audio_mix_count{};
    int audio_tracks{};
    enum AVPixelFormat format{};
    enum AVColorRange color_range{};
    enum AVColorPrimaries color_primaries{};
    enum AVColorTransferCharacteristic color_trc{};
    enum AVColorSpace colorspace{};
    int max_luminance{};
    int scale_width{};
    int scale_height{};
    int width{};
    int height{};
    int frame_size{}; // audio frame size
};

struct ffmpeg_audio_info {
    AVStream *stream{};
    AVCodecContext *ctx{};
};

struct ffmpeg_data {
    AVStream *video{};
    AVCodecContext *video_ctx{};
    std::vector<ffmpeg_audio_info> audio_infos{};
    const AVCodec *acodec{};
    const AVCodec *vcodec{};
    AVFormatContext *output{};
    struct SwsContext *swscale{};

    int64_t total_frames{};
    AVFrame *vframe{};
    int frame_size{};

    uint64_t start_timestamp{};

    int64_t total_samples[MAX_AUDIO_MIXES]{};
    uint32_t audio_samplerate{};
    enum audio_format audio_format{};
    size_t audio_planes{};
    size_t audio_size{};
    int num_audio_streams{};

    /* audio_tracks is a bitmask storing the indices of the mixes */
    int audio_tracks{};
    struct circlebuf excess_frames[MAX_AUDIO_MIXES][MAX_AV_PLANES]{};
    uint8_t *samples[MAX_AUDIO_MIXES][MAX_AV_PLANES]{};
    AVFrame *aframe[MAX_AUDIO_MIXES]{};

    struct ffmpeg_cfg config{};

    bool initialized{};

    std::string last_error{};
};
struct mpeg_ts_output_private
{
    std::string stream_info{};
    std::string cdn_ip{};
    bool initilized{};

    std::atomic_bool active{};
    ffmpeg_data *ff_data{};

    bool connecting{};
    std::thread start_thread;

    uint64_t total_bytes{};

    uint64_t audio_start_ts{};
    uint64_t video_start_ts{};
    uint64_t stop_ts{};
    std::atomic_bool stopping{};

    bool write_thread_active{};
    std::mutex write_mutex;
    std::thread write_thread;
    os_sem_t *write_sem{};
    os_event_t *stop_event{};

    std::list<AVPacket *> packets{};

    /* used for SRT & RIST */
    URLContext *h{};
    AVIOContext *s{};
    bool got_headers{};
    bool sent_first_media_packet{};
};

mpeg_ts_output::mpeg_ts_output()
{
    d_ptr = std::make_unique<mpeg_ts_output_private>();
}

void mpeg_ts_output::ffmpeg_mpegts_log_error(int log_level, struct ffmpeg_data *data, const char *format, ...)
{
    va_list args;
    char out[4096];

    va_start(args, format);
    vsnprintf(out, sizeof(out), format, args);
    va_end(args);

    data->last_error = out;

    blog(log_level, "%s", out);
}

void mpeg_ts_output::i_set_output_info(const std::string &info)
{
    d_ptr->stream_info = info;
}

bool mpeg_ts_output::i_output_valid()
{
    return d_ptr->initilized;
}

bool mpeg_ts_output::i_has_video()
{
    return true;
}

bool mpeg_ts_output::i_has_audio()
{
    return true;
}

bool mpeg_ts_output::i_encoded()
{
    return true;
}

bool mpeg_ts_output::i_create()
{
    if (os_event_init(&d_ptr->stop_event, OS_EVENT_TYPE_AUTO) != 0)
        return false;

    if (os_sem_init(&d_ptr->write_sem, 0) != 0)
        return false;

    d_ptr->initilized = true;
    return true;
}

void mpeg_ts_output::i_destroy()
{
    if (d_ptr->start_thread.joinable())
        d_ptr->start_thread.join();

    full_stop();

    os_sem_destroy(d_ptr->write_sem);
    os_event_destroy(d_ptr->stop_event);

    d_ptr->initilized = false;
}

void mpeg_ts_output::start_thread(void *data)
{
    auto output = (mpeg_ts_output *)data;
    output->start_internal();
}

bool mpeg_ts_output::ffmpeg_mpegts_data_init(ffmpeg_data *data, ffmpeg_cfg *config)
{
    data->config = *config;
    data->num_audio_streams = config->audio_mix_count;
    data->audio_tracks = config->audio_tracks;

    if (!config->url || !*config->url)
        return false;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
#endif
    avformat_network_init();

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 0, 100)
    AVOutputFormat *output_format;
#else
    const AVOutputFormat *output_format;
#endif

    output_format = av_guess_format("mpegts", NULL, "video/M2PT");

    if (output_format == NULL) {
        ffmpeg_mpegts_log_error(LOG_WARNING, data,
                                "Couldn't set output format to mpegts");
        goto fail;
    } else {
        blog(LOG_INFO, "info: Output format name and long_name: %s, %s\n", output_format->name ? output_format->name : "unknown", output_format->long_name ? output_format->long_name : "unknown");
    }

    avformat_alloc_output_context2(&data->output, output_format, NULL, data->config.url);

    if (!data->output) {
        ffmpeg_mpegts_log_error(LOG_WARNING, data, "Couldn't create avformat context");
        goto fail;
    }

    return true;

fail:
    blog(LOG_WARNING, "ffmpeg_data_init failed");
    return false;
}

bool mpeg_ts_output::new_stream(ffmpeg_data *data, struct AVStream **stream, const char *name)
{
    *stream = avformat_new_stream(data->output, NULL);
    if (!*stream) {
        ffmpeg_mpegts_log_error(LOG_WARNING, data, "Couldn't create stream for encoder '%s'", name);
        return false;
    }

    (*stream)->id = data->output->nb_streams - 1;
    return true;
}

bool mpeg_ts_output::create_video_stream(ffmpeg_data *data)
{
    auto video = lite_obs_output_video();
    if (!video) {
        ffmpeg_mpegts_log_error(LOG_WARNING, data, "No active video");
        return false;
    }
    const char *name = data->config.video_encoder;
    const AVCodecDescriptor *codec = avcodec_descriptor_get_by_name(name);
    if (!codec) {
        blog(LOG_ERROR, "Couldn't find codec '%s'\n", name);
        return false;
    }
    if (!new_stream(data, &data->video, name))
        return false;

    auto ovi = video->video_output_get_info();
    auto context = avcodec_alloc_context3(NULL);
    context->codec_type = codec->type;
    context->codec_id = codec->id;
    context->bit_rate = (int64_t)data->config.video_bitrate * 1000;
    context->width = data->config.scale_width;
    context->height = data->config.scale_height;
    context->coded_width = data->config.scale_width;
    context->coded_height = data->config.scale_height;
    context->time_base = {(int)ovi->fps_den, (int)ovi->fps_num};
    context->gop_size = data->config.gop_size;
    context->pix_fmt = data->config.format;
    context->color_range = data->config.color_range;
    context->color_primaries = data->config.color_primaries;
    context->color_trc = data->config.color_trc;
    context->colorspace = data->config.colorspace;
    context->chroma_sample_location = (data->config.colorspace == AVCOL_SPC_BT2020_NCL) ? AVCHROMA_LOC_TOPLEFT : AVCHROMA_LOC_LEFT;
    context->thread_count = 0;

    data->video->time_base = context->time_base;
#if LIBAVFORMAT_VERSION_MAJOR < 59
    data->video->codec->time_base = context->time_base;
#endif
    data->video->avg_frame_rate = av_inv_q(context->time_base);

    data->video_ctx = context;
    data->config.width = data->config.scale_width;
    data->config.height = data->config.scale_height;

    avcodec_parameters_from_context(data->video->codecpar, context);
    return true;
}

bool mpeg_ts_output::create_audio_stream(ffmpeg_data *data, int idx)
{
    AVCodecContext *context;
    AVStream *avstream;
    const char *name = data->config.audio_encoder;
    const AVCodecDescriptor *codec = avcodec_descriptor_get_by_name(name);
    if (!codec) {
        blog(LOG_WARNING, "Couldn't find codec '%s'\n", name);
        return false;
    }

    auto audio = lite_obs_output_audio();
    if (!audio) {
        ffmpeg_mpegts_log_error(LOG_WARNING, data, "No active audio");
        return false;
    }

    if (!new_stream(data, &avstream, data->config.audio_encoder))
        return false;

    auto aoi = audio->audio_output_get_info();
    context = avcodec_alloc_context3(NULL);
    context->codec_type = codec->type;
    context->codec_id = codec->id;
    context->bit_rate = (int64_t)data->config.audio_bitrate * 1000;
    context->time_base = {1, (int)aoi->samples_per_sec};
    context->channels = get_audio_channels(aoi->speakers);
    context->sample_rate = aoi->samples_per_sec;
    context->channel_layout = av_get_default_channel_layout(context->channels);

    //avutil default channel layout for 5 channels is 5.0 ; fix for 4.1
    if (aoi->speakers == speaker_layout::SPEAKERS_4POINT1)
        context->channel_layout = av_get_channel_layout("4.1");

    context->sample_fmt = AV_SAMPLE_FMT_S16;
    context->frame_size = data->config.frame_size;

    avstream->time_base = context->time_base;

    data->audio_samplerate = aoi->samples_per_sec;
    data->audio_format = convert_ffmpeg_sample_format(context->sample_fmt);
    data->audio_planes = get_audio_planes(data->audio_format, aoi->speakers);
    data->audio_size = get_audio_size(data->audio_format, aoi->speakers, 1);

    data->audio_infos[idx].stream = avstream;
    data->audio_infos[idx].ctx = context;
    avcodec_parameters_from_context(data->audio_infos[idx].stream->codecpar, context);
    return true;
}

bool mpeg_ts_output::init_streams(ffmpeg_data *data)
{
    if (!create_video_stream(data))
        return false;

    if (data->num_audio_streams) {
        data->audio_infos.resize(data->num_audio_streams);
        for (int i = 0; i < data->num_audio_streams; i++) {
            if (!create_audio_stream(data, i))
                return false;
        }
    }

    return true;
}

int ff_network_init(void)
{
#if HAVE_WINSOCK2_H
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(1, 1), &wsaData))
        return 0;
#endif
    return 1;
}

int mpeg_ts_output::connect_mpegts_url()
{
    const char *url = d_ptr->ff_data->config.url;
    if (!ff_network_init()) {
        ffmpeg_mpegts_log_error(LOG_ERROR, d_ptr->ff_data, "Can not initialize network.");
        return AVERROR(EIO);
    }

    URLContext *uc = nullptr;
    int err = 0;
    do {
        uc = (URLContext *)av_mallocz(sizeof(URLContext) + strlen(url) + 1);
        if (!uc) {
            ffmpeg_mpegts_log_error(LOG_ERROR, d_ptr->ff_data, "Can not allocate memory.");
            break;
        }
        uc->url = (char *)url;
        uc->max_packet_size = SRT_LIVE_DEFAULT_PAYLOAD_SIZE;
        uc->priv_data = av_mallocz(sizeof(SRTContext));
        if (!uc->priv_data) {
            ffmpeg_mpegts_log_error(LOG_ERROR, d_ptr->ff_data, "Can not allocate memory.");
            break;
        }
        d_ptr->h = uc;

        err = libsrt_open(uc, uc->url);
        if (err < 0)
            break;

        d_ptr->cdn_ip = uc->cdn_addr;
        return 0;
    } while (0);

    if (uc)
        av_freep(&uc->priv_data);
    av_freep(&uc);
#if HAVE_WINSOCK2_H
    WSACleanup();
#endif
    return err;
}

void mpeg_ts_output::close_mpegts_url()
{
    AVIOContext *s = d_ptr->s;
    if (!s)
        return;
    URLContext *h = (URLContext *)s->opaque;
    if (!h)
        return; /* can happen when opening the url fails */

    /* close rist or srt URLs ; free URLContext */

    auto err = libsrt_close(h);

    av_freep(&h->priv_data);
    av_freep(h);

    /* close custom avio_context for srt or rist */
    avio_flush(d_ptr->s);
    d_ptr->s->opaque = NULL;
    av_freep(&d_ptr->s->buffer);
    avio_context_free(&d_ptr->s);

    if (err)
        blog(LOG_INFO, "[ffmpeg mpegts muxer:] Error closing URL %s", d_ptr->ff_data->config.url);
}

int mpeg_ts_output::allocate_custom_aviocontext()
{
    /* allocate buffers */
    URLContext *h = d_ptr->h;
    auto buffer_size = UDP_DEFAULT_PAYLOAD_SIZE;
    auto buffer = (uint8_t *)av_malloc(buffer_size);
    if (!buffer)
        return AVERROR(ENOMEM);
    /* allocate custom avio_context */

    auto s = avio_alloc_context(buffer, buffer_size, AVIO_FLAG_WRITE, h, NULL, (int (*)(void *, uint8_t *, int))libsrt_write, NULL);
    if (!s)
        goto fail;
    s->max_packet_size = h->max_packet_size;
    s->opaque = h;
    d_ptr->s = s;
    d_ptr->ff_data->output->pb = s;

    return 0;
fail:
    av_freep(&buffer);
    return AVERROR(ENOMEM);
}

int mpeg_ts_output::open_output_file(ffmpeg_data *data)
{
    int ret = -1;
    bool srt = is_srt(data->config.url);
    bool allowed_proto = proto_is_allowed(data->config.url);
    AVDictionary *dict = NULL;

    /* Retrieve protocol settings for udp, tcp, rtp ... (not srt or rist).
     * These options will be passed to protocol by avio_open2 through dict.
     * The invalid options will be left in dict. */
    if (!srt) {
        if ((ret = av_dict_parse_string(&dict, data->config.protocol_settings, "=", " ", 0))) {
            ffmpeg_mpegts_log_error(
                        LOG_WARNING, data,
                        "Failed to parse protocol settings: %d\n%s",
                        ret,
                        data->config.protocol_settings);

            av_dict_free(&dict);
            return OBS_OUTPUT_INVALID_STREAM;
        }
    }

    /* Ensure h264 bitstream auto conversion from avcc to annex B */
    data->output->flags |= AVFMT_FLAG_AUTO_BSF;

    /* Open URL for rist, srt or other protocols compatible with mpegts
     *  muxer supported by avformat (udp, tcp, rtp ...).
     */
    if (srt) {
        ret = connect_mpegts_url();
    } else if (allowed_proto) {
        ret = avio_open2(&data->output->pb, data->config.url, AVIO_FLAG_WRITE, NULL, &dict);
    } else {
        blog(LOG_INFO, "[ffmpeg mpegts muxer:] Invalid protocol: %s", data->config.url);
        return OBS_OUTPUT_BAD_PATH;
    }

    if (ret < 0) {
        if (srt && (ret == OBS_OUTPUT_CONNECT_FAILED || ret == OBS_OUTPUT_INVALID_STREAM)) {
            blog(LOG_ERROR, "failed to open the url or invalid stream");
        } else {
            ffmpeg_mpegts_log_error(LOG_WARNING, data,
                                    "Couldn't open '%s', %d",
                                    data->config.url,
                                    ret);
            av_dict_free(&dict);
        }
        return ret;
    }

    /* Log invalid protocol settings for all protocols except srt or rist.
     * Or for srt & rist, allocate custom avio_ctx which will host the
     * protocols write callbacks.
     */
    if (!srt) {
        av_dict_free(&dict);
    } else {
        ret = allocate_custom_aviocontext();
        if (ret < 0) {
            blog(LOG_INFO, "Couldn't allocate custom avio_context for rist or srt'%s', %d\n", data->config.url, ret);
            return OBS_OUTPUT_INVALID_STREAM;
        }
    }

    auto callback = output_signal_callback();
    if (callback)
        callback->connected();

    return 0;
}

uint64_t mpeg_ts_output::get_packet_sys_dts(struct AVPacket *packet)
{
    auto data = d_ptr->ff_data;
    uint64_t pause_offset = 0;
    uint64_t start_ts;

    AVRational time_base;

    if (data->video && data->video->index == packet->stream_index) {
        time_base = data->video->time_base;
        start_ts = d_ptr->video_start_ts;
    } else {
        time_base = data->audio_infos[0].stream->time_base;
        start_ts = d_ptr->audio_start_ts;
    }

    return start_ts + pause_offset + (uint64_t)av_rescale_q(packet->dts, time_base, {1, 1000000000});
}

int mpeg_ts_output::mpegts_process_packet()
{
    AVPacket *packet = nullptr;
    uint8_t *buf = nullptr;
    int ret = 0;

    d_ptr->write_mutex.lock();
    if (!d_ptr->packets.empty()) {
        packet = d_ptr->packets.front();
        d_ptr->packets.pop_front();
    }
    d_ptr->write_mutex.unlock();

    if (!packet)
        return 0;

    if (d_ptr->stopping) {
        uint64_t sys_ts = get_packet_sys_dts(packet);
        if (sys_ts >= d_ptr->stop_ts) {
            ret = 0;
            goto end;
        }
    }
    d_ptr->total_bytes += packet->size;
    buf = packet->data;
    ret = av_interleaved_write_frame(d_ptr->ff_data->output, packet);
    av_freep(&buf);

    if (ret < 0) {
        ffmpeg_mpegts_log_error(
                    LOG_WARNING, d_ptr->ff_data,
                    "process_packet: Error writing packet: %d",
                    ret);

        /* Treat "Invalid data found when processing input" and
         * "Invalid argument" as non-fatal */
        if (ret == AVERROR_INVALIDDATA || ret == -EINVAL) {
            ret = 0;
        }
    }
end:
    av_packet_free(&packet);
    return ret;
}

void mpeg_ts_output::write_internal()
{
    while (os_sem_wait(d_ptr->write_sem) == 0) {
        /* check to see if shutting down */
        if (os_event_try(d_ptr->stop_event) == 0)
            break;

        int ret = mpegts_process_packet(); // todo
        if (ret != 0) {
            int code = OBS_OUTPUT_DISCONNECTED;

            d_ptr->write_thread.detach();
            d_ptr->write_thread_active = false;

            if (ret == -ENOSPC)
                code = OBS_OUTPUT_NO_SPACE;

            lite_obs_output_signal_stop(code);
            ffmpeg_mpegts_deactivate();
            break;
        } else {
            if (!d_ptr->sent_first_media_packet) {
                d_ptr->sent_first_media_packet = true;
                auto callback = output_signal_callback();
                if (callback)
                    callback->first_media_packet();
            }
        }
    }

    d_ptr->active = false;
}

void mpeg_ts_output::write_thread(void *data)
{
    auto output = (mpeg_ts_output *)data;
    output->write_internal();
}

bool mpeg_ts_output::set_config()
{
    struct ffmpeg_cfg config;
    config.url = d_ptr->stream_info.c_str();
    config.format_name = "mpegts";
    config.format_mime_type = "video/M2PT";

    /* 2. video settings */
    // 2.a) set video format from obs to FFmpeg
    auto video = lite_obs_output_video();
    if (!video)
        return false;

    auto video_info = video->video_output_get_info();
    config.format = lite_obs_to_ffmpeg_video_format(video_info->format);

    if (config.format == AV_PIX_FMT_NONE) {
        blog(LOG_DEBUG, "invalid pixel format used for mpegts output");
        return false;
    }

    // 2.b) set colorspace, color_range & transfer characteristic (from voi)
    config.color_range = video_info->range == video_range_type::VIDEO_RANGE_FULL ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    config.colorspace = format_is_yuv(video_info->format) ? AVCOL_SPC_BT709 : AVCOL_SPC_RGB;
    switch (video_info->colorspace) {
    case video_colorspace::VIDEO_CS_DEFAULT:
    case video_colorspace::VIDEO_CS_601:
        config.color_primaries = AVCOL_PRI_SMPTE170M;
        config.color_trc = AVCOL_TRC_SMPTE170M;
        config.colorspace = AVCOL_SPC_SMPTE170M;
        break;
    case video_colorspace::VIDEO_CS_709:
        config.color_primaries = AVCOL_PRI_BT709;
        config.color_trc = AVCOL_TRC_BT709;
        config.colorspace = AVCOL_SPC_BT709;
        break;
    }

    // 2.c) set width & height
    config.width = (int)lite_obs_output_get_width();
    config.height = (int)lite_obs_output_get_height();
    config.scale_width = config.width;
    config.scale_height = config.height;

    // 2.d) set video codec & id from video encoder
    auto vencoder = lite_obs_output_get_video_encoder();
    config.video_encoder = vencoder->i_encoder_codec();
    if (strcmp(config.video_encoder, "h264") == 0)
        config.video_encoder_id = AV_CODEC_ID_H264;
    else
        config.video_encoder_id = AV_CODEC_ID_AV1;

    // 2.e)  set video bitrate & gop through video encoder settings
    config.video_bitrate = vencoder->lite_obs_encoder_bitrate();
    int keyint_sec = 2;
    config.gop_size = keyint_sec ? keyint_sec * video_info->fps_num / video_info->fps_den : 250;

    /* 3. Audio settings */
    // 3.a) set audio encoder and id to aac
    auto aencoder = lite_obs_output_get_audio_encoder(0);
    config.audio_encoder = "aac";
    config.audio_encoder_id = AV_CODEC_ID_AAC;

    // 3.b) get audio bitrate from the audio encoder.
    config.audio_bitrate = aencoder->lite_obs_encoder_bitrate();

    // 3.c set audio frame size
    config.frame_size = (int)aencoder->lite_obs_encoder_get_frame_size();

    // 3.d) set the number of tracks
    // The UI for multiple tracks is not written for streaming outputs.
    // When it is, modify write_packet & uncomment :
    // config.audio_tracks = (int)obs_output_get_mixers(stream->output);
    // config.audio_mix_count = get_audio_mix_count(config.audio_tracks);
    config.audio_tracks = 1;
    config.audio_mix_count = 1;

    /* 4. Muxer & protocol settings */
    config.muxer_settings = "";
    config.protocol_settings = "";

    /* 5. unused ffmpeg codec settings */
    config.video_settings = "";
    config.audio_settings = "";

    d_ptr->ff_data = new ffmpeg_data;
    auto ff_data = d_ptr->ff_data;
    int code = -1;
    auto success = ffmpeg_mpegts_data_init(d_ptr->ff_data, &config);
    if (!success) {
        if (!d_ptr->ff_data->last_error.empty()) {
            lite_obs_output_set_last_error(d_ptr->ff_data->last_error);
        }
        ffmpeg_mpegts_data_free(&d_ptr->ff_data);
        code = OBS_OUTPUT_INVALID_STREAM;
        goto fail;
    }

    if (!d_ptr->got_headers) {
        if (!init_streams(ff_data)) {
            blog(LOG_ERROR, "mpegts avstream failed to be created");
            code = OBS_OUTPUT_INVALID_STREAM;
            goto fail;
        }
        code = open_output_file(ff_data);
        if (code != 0) {
            blog(LOG_ERROR, "failed to open the url");
            goto fail;
        }
        av_dump_format(ff_data->output, 0, NULL, 1);
    }
    if (!lite_obs_output_can_begin_data_capture())
        return false;
    if (!lite_obs_output_initialize_encoders())
        return false;

    d_ptr->write_thread = std::thread(mpeg_ts_output::write_thread, this);
    d_ptr->active = true;
    d_ptr->write_thread_active = true;
    d_ptr->total_bytes = 0;
    lite_obs_output_begin_data_capture();

    return true;
fail:
    lite_obs_output_signal_stop(code);
    full_stop();
    return false;
}

void mpeg_ts_output::start_internal()
{
    set_config();
    d_ptr->connecting = false;
}

void mpeg_ts_output::full_stop()
{
    if (d_ptr->active) {
        lite_obs_output_end_data_capture();
        ffmpeg_mpegts_deactivate();
    }
}

bool mpeg_ts_output::i_start()
{
    if (d_ptr->connecting)
        return false;


    d_ptr->stopping = false;
    d_ptr->audio_start_ts = 0;
    d_ptr->video_start_ts = 0;
    d_ptr->total_bytes = 0;
    d_ptr->got_headers = false;
    d_ptr->sent_first_media_packet = false;

    d_ptr->start_thread = std::thread(mpeg_ts_output::start_thread, this);
    d_ptr->connecting = true;

    return true;
}

void mpeg_ts_output::close_video(ffmpeg_data *data)
{
    avcodec_free_context(&data->video_ctx);
}

void mpeg_ts_output::close_audio(ffmpeg_data *data)
{
    for (int idx = 0; idx < data->num_audio_streams; idx++) {
        for (size_t i = 0; i < MAX_AV_PLANES; i++)
            circlebuf_free(&data->excess_frames[idx][i]);

        if (data->samples[idx][0])
            av_freep(&data->samples[idx][0]);
        if (data->audio_infos[idx].ctx) {
            avcodec_free_context(&data->audio_infos[idx].ctx);
        }
        if (data->aframe[idx])
            av_frame_free(&data->aframe[idx]);
    }
}

void mpeg_ts_output::ffmpeg_mpegts_data_free(ffmpeg_data **d)
{
    auto data = *d;
    if (data->initialized)
        av_write_trailer(data->output);

    if (data->video)
        close_video(data);
    if (!data->audio_infos.empty()) {
        close_audio(data);
        data->audio_infos.clear();
    }

    if (data->output) {
        if (is_srt(data->config.url)) {
            close_mpegts_url();
        } else {
            avio_close(data->output->pb);
        }
        avformat_free_context(data->output);
        data->video = nullptr;
        data->output = nullptr;
        data->num_audio_streams = 0;
    }

    data->last_error.clear();

    delete data;
    data = nullptr;
}

void mpeg_ts_output::i_stop(uint64_t ts)
{
    if (d_ptr->active) {
        if (ts > 0) {
            d_ptr->stop_ts = ts;
            d_ptr->stopping = true;
        }

        full_stop();
    } else {
        lite_obs_output_signal_stop(OBS_OUTPUT_SUCCESS);
    }
}

void mpeg_ts_output::i_raw_video(video_data *frame)
{

}

void mpeg_ts_output::i_raw_audio(audio_data *frames)
{

}

bool mpeg_ts_output::get_video_headers(ffmpeg_data *data)
{
    AVCodecParameters *par = data->video->codecpar;
    auto vencoder = lite_obs_output_get_video_encoder();

    uint8_t *data_p = nullptr;
    size_t size = 0;
    if (vencoder->i_get_extra_data(&data_p, &size)) {
        par->extradata = (uint8_t *)av_memdup(data_p, size);
        par->extradata_size = (int)size;
        avcodec_parameters_to_context(data->video_ctx, data->video->codecpar);
        return 1;
    }
    return 0;
}

bool mpeg_ts_output::get_audio_headers(ffmpeg_data *data, int idx)
{
    AVCodecParameters *par = data->audio_infos[idx].stream->codecpar;
    auto aencoder = lite_obs_output_get_audio_encoder(idx);

    uint8_t *data_p = nullptr;
    size_t size = 0;
    if (aencoder->i_get_extra_data(&data_p, &size)) {
        par->extradata = (uint8_t *)av_memdup(data_p, size);
        par->extradata_size = (int)size;
        avcodec_parameters_to_context(data->audio_infos[idx].ctx, par);
        return 1;
    }
    return 0;
}

bool mpeg_ts_output::get_extradata()
{
    /* get extradata for av headers from encoders */
    if (!get_video_headers(d_ptr->ff_data))
        return false;
    for (int i = 0; i < d_ptr->ff_data->num_audio_streams; i++) {
        if (!get_audio_headers(d_ptr->ff_data, i))
            return false;
    }

    return true;
}

bool mpeg_ts_output::write_header(struct ffmpeg_data *data)
{
    AVDictionary *dict = NULL;
    int ret;
    /* get mpegts muxer settings (can be used with rist, srt, rtp, etc ... */
    if ((ret = av_dict_parse_string(&dict, data->config.muxer_settings, "=",
                                    " ", 0))) {
        ffmpeg_mpegts_log_error(
                    LOG_WARNING, data,
                    "Failed to parse muxer settings: %d\n%s",
                    ret, data->config.muxer_settings);

        av_dict_free(&dict);
        return false;
    }

    /* Allocate the stream private data and write the stream header. */
    ret = avformat_write_header(data->output, &dict);
    if (ret < 0) {
        ffmpeg_mpegts_log_error(
                    LOG_WARNING, data,
                    "Error setting stream header for '%s': %d",
                    data->config.url, ret);
        return false;
    }

    av_dict_free(&dict);
    return true;
}

static inline int64_t rescale_ts2(AVStream *stream, AVRational codec_time_base,
                                  int64_t val)
{
    return av_rescale_q_rnd(val / codec_time_base.num, codec_time_base,
                            stream->time_base,
                            (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
}

void mpeg_ts_output::mpegts_write_packet(std::shared_ptr<encoder_packet> encpacket)
{
    if (d_ptr->stopping || !d_ptr->ff_data->video || !d_ptr->ff_data->video_ctx || d_ptr->ff_data->audio_infos.empty())
        return;
    if (!d_ptr->ff_data->audio_infos[encpacket->track_idx].stream)
        return;
    bool is_video = encpacket->type == obs_encoder_type::OBS_ENCODER_VIDEO;
    AVStream *avstream =
            is_video ? d_ptr->ff_data->video
                     : d_ptr->ff_data->audio_infos[encpacket->track_idx].stream;
    AVPacket *packet = NULL;

    const AVRational codec_time_base =
            is_video ? d_ptr->ff_data->video_ctx->time_base
                     : d_ptr->ff_data->audio_infos[encpacket->track_idx].ctx->time_base;

    packet = av_packet_alloc();

    packet->data = (uint8_t *)av_memdup(encpacket->data->data(), (int)encpacket->data->size());
    if (packet->data == NULL) {
        blog(LOG_ERROR, "couldn't allocate packet data");
        goto fail;
    }
    packet->size = (int)encpacket->data->size();
    packet->stream_index = avstream->id;
    packet->pts = rescale_ts2(avstream, codec_time_base, encpacket->pts);
    packet->dts = rescale_ts2(avstream, codec_time_base, encpacket->dts);

    if (encpacket->keyframe)
        packet->flags = AV_PKT_FLAG_KEY;

    d_ptr->write_mutex.lock();
    d_ptr->packets.push_back(packet);
    d_ptr->write_mutex.unlock();
    os_sem_post(d_ptr->write_sem);
    return;
fail:
    av_packet_free(&packet);
}

void mpeg_ts_output::i_encoded_packet(std::shared_ptr<encoder_packet> packet)
{
    auto ff_data = d_ptr->ff_data;
    int code;
    if (!d_ptr->got_headers) {
        if (get_extradata()) {
            d_ptr->got_headers = true;
        } else {
            blog(LOG_WARNING, "failed to retrieve headers");
            code = OBS_OUTPUT_INVALID_STREAM;
            goto fail;
        }
        if (!write_header(ff_data)) {
            blog(LOG_ERROR, "failed to write headers");
            code = OBS_OUTPUT_INVALID_STREAM;
            goto fail;
        }
        av_dump_format(ff_data->output, 0, NULL, 1);
        ff_data->initialized = true;
    }

    if (!d_ptr->active)
        return;

    /* encoder failure */
    if (!packet) {
        lite_obs_output_signal_stop(OBS_OUTPUT_ENCODE_ERROR);
        ffmpeg_mpegts_deactivate();
        return;
    }

    if (d_ptr->stopping) {
        if (packet->sys_dts_usec >= (int64_t)d_ptr->stop_ts) {
            ffmpeg_mpegts_deactivate();
            return;
        }
    }

    mpegts_write_packet(packet);
    return;
fail:
    lite_obs_output_signal_stop(code);
    full_stop();
}

uint64_t mpeg_ts_output::i_get_total_bytes()
{
    return d_ptr->total_bytes;
}

int mpeg_ts_output::i_get_dropped_frames()
{
    return 0;
}

std::string mpeg_ts_output::i_cdn_ip()
{
    return d_ptr->cdn_ip;
}

void mpeg_ts_output::ffmpeg_mpegts_deactivate()
{
    if (d_ptr->write_thread_active) {
        os_event_signal(d_ptr->stop_event);
        os_sem_post(d_ptr->write_sem);
        d_ptr->write_thread_active = false;
    }

    if (d_ptr->write_thread.joinable())
        d_ptr->write_thread.join();

    d_ptr->write_mutex.lock();

    for (auto iter = d_ptr->packets.begin(); iter != d_ptr->packets.end(); iter++) {
        auto packet = *iter;
        av_packet_free(&packet);
    }
    d_ptr->packets.clear();

    d_ptr->write_mutex.unlock();

    ffmpeg_mpegts_data_free(&d_ptr->ff_data);
}
