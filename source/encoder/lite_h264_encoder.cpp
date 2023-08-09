#include "lite-obs/encoder/lite_h264_encoder.h"
#include "lite-obs/media-io/ffmpeg_formats.h"
#include "lite-obs/lite_obs_avc.h"

#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include "lite-obs/media-io/video_output.h"
#include "lite-obs/util/log.h"
#include "lite-obs/lite_obs_platform_config.h"

static inline bool valid_format(video_format format)
{
    return format == video_format::VIDEO_FORMAT_I420 || format == video_format::VIDEO_FORMAT_NV12 || format == video_format::VIDEO_FORMAT_I444;
}

struct lite_ffmpeg_video_encoder_private
{
    const AVCodec *codec{};
    AVCodecContext *context{};

    AVFrame *vframe{};

    std::shared_ptr<std::vector<uint8_t>> buffer{};
    std::vector<uint8_t> header{};
    std::vector<uint8_t> sei{};

    int height{};
    bool first_packet{};
    bool initialized{};

    lite_ffmpeg_video_encoder_private() {
        buffer = std::make_shared<std::vector<uint8_t>>();
    }
};

lite_h264_video_encoder::lite_h264_video_encoder(int bitrate, size_t mixer_idx)
    : lite_obs_encoder(bitrate, mixer_idx)
{
    d_ptr = std::make_unique<lite_ffmpeg_video_encoder_private>();
}

lite_h264_video_encoder::~lite_h264_video_encoder()
{

}

const char *lite_h264_video_encoder::i_encoder_codec()
{
    return "h264";
}

obs_encoder_type lite_h264_video_encoder::i_encoder_type()
{
    return obs_encoder_type::OBS_ENCODER_VIDEO;
}

bool lite_h264_video_encoder::i_encoder_valid()
{
    return d_ptr->initialized;
}

bool lite_h264_video_encoder::i_create()
{
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    avcodec_register_all();
#endif

#if TARGET_PLATFORM == PLATFORM_WIN32
    d_ptr->codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!d_ptr->codec)
        d_ptr->codec = avcodec_find_encoder_by_name("nvenc_h264");
#elif TARGET_PLATFORM == PLATFORM_ANDROID
    d_ptr->codec = avcodec_find_encoder_by_name("h264_mediacodec");
#elif TARGET_PLATFORM == PLATFORM_MAC
    d_ptr->codec = avcodec_find_encoder_by_name("h264_videotoolbox");
#endif
    d_ptr->first_packet = true;

    blog(LOG_INFO, "---------------------------------");

    if (!d_ptr->codec) {
        blog(LOG_WARNING, "Couldn't find encoder");
        goto fail;
    }

    d_ptr->context = avcodec_alloc_context3(d_ptr->codec);
    if (!d_ptr->context) {
        blog(LOG_WARNING, "Failed to create codec context");
        goto fail;
    }

    if (!update_settings())
        goto fail;

    return true;

fail:
    i_destroy();
    return false;
}

void lite_h264_video_encoder::i_destroy()
{
    if (d_ptr->initialized) {
        AVPacket pkt = {0};
        int r_pkt = 1;

        while (r_pkt) {
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 40, 101)
            if (avcodec_receive_packet(d_ptr->context, &pkt) < 0)
                break;
#else
            if (avcodec_encode_video2(d_ptr->context, &pkt, NULL, &r_pkt) < 0)
                break;
#endif

            if (r_pkt)
                av_packet_unref(&pkt);
        }
    }

    avcodec_close(d_ptr->context);
    av_frame_unref(d_ptr->vframe);
    av_frame_free(&d_ptr->vframe);
    d_ptr->buffer->clear();
    d_ptr->header.clear();
    d_ptr->sei.clear();

    d_ptr->initialized = false;
}

static inline void copy_data(AVFrame *pic, const struct encoder_frame *frame, int height, AVPixelFormat format)
{
    int h_chroma_shift, v_chroma_shift;
    av_pix_fmt_get_chroma_sub_sample(format, &h_chroma_shift,
                                     &v_chroma_shift);
    for (int plane = 0; plane < MAX_AV_PLANES; plane++) {
        if (!frame->data[plane])
            continue;

        int frame_rowsize = (int)frame->linesize[plane];
        int pic_rowsize = pic->linesize[plane];
        int bytes = frame_rowsize < pic_rowsize ? frame_rowsize
                                                : pic_rowsize;
        int plane_height = height >> (plane ? v_chroma_shift : 0);

        for (int y = 0; y < plane_height; y++) {
            int pos_frame = y * frame_rowsize;
            int pos_pic = y * pic_rowsize;

            memcpy(pic->data[plane] + pos_pic,
                   frame->data[plane] + pos_frame, bytes);
        }
    }
}

bool lite_h264_video_encoder::i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void(std::shared_ptr<encoder_packet>)> send_off)
{
    AVPacket av_pkt{};
    av_init_packet(&av_pkt);

    copy_data(d_ptr->vframe, frame, d_ptr->height, d_ptr->context->pix_fmt);

    d_ptr->vframe->pts = frame->pts;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 40, 101)
    auto ret = avcodec_send_frame(d_ptr->context, d_ptr->vframe);
    if (ret == 0)
again:
        ret = avcodec_receive_packet(d_ptr->context, &av_pkt);

    auto got_packet = (ret == 0);

    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
        ret = 0;
#else
    ret = avcodec_encode_video2(d_ptr->context, &av_pkt, d_ptr->vframe, &got_packet);
#endif
    if (ret < 0) {
        blog(LOG_WARNING, "encode: Error encoding: %d", ret);
        return false;
    }

    if (got_packet && av_pkt.size) {
        if (d_ptr->first_packet) {
            d_ptr->first_packet = false;
            obs_extract_avc_headers(av_pkt.data, av_pkt.size, d_ptr->buffer, d_ptr->header, d_ptr->sei);
        } else {
            d_ptr->buffer->resize(av_pkt.size);
            memcpy(d_ptr->buffer->data(), av_pkt.data, av_pkt.size);
        }

        uint8_t sei_buf[102400] = {0};
        int sei_len = 0;
        bool got_sei = lite_obs_encoder_get_sei(sei_buf, &sei_len);
        if (got_sei) {
            auto old_num = d_ptr->buffer->size();
            d_ptr->buffer->resize(old_num + sei_len);
            memcpy(d_ptr->buffer->data() + old_num, sei_buf, sei_len);
        }

        packet->pts = av_pkt.pts;
        packet->dts = av_pkt.dts;
        packet->data = d_ptr->buffer;
        packet->type = obs_encoder_type::OBS_ENCODER_VIDEO;
        packet->keyframe = obs_avc_keyframe(packet->data->data(), packet->data->size());
        send_off(packet);

        goto again;
    }

    av_packet_unref(&av_pkt);
    return true;
}

bool lite_h264_video_encoder::i_get_extra_data(uint8_t **extra_data, size_t *size)
{
    *extra_data = d_ptr->header.data();
    *size = d_ptr->header.size();
    return true;
}

bool lite_h264_video_encoder::i_get_sei_data(uint8_t **sei_data, size_t *size)
{
    *sei_data = d_ptr->sei.data();
    *size = d_ptr->sei.size();
    return true;
}

void lite_h264_video_encoder::i_get_audio_info(audio_convert_info *info)
{
    return;
}

void lite_h264_video_encoder::i_get_video_info(video_scale_info *info)
{
    video_format pref_format = lite_obs_encoder_get_preferred_video_format();
    if (!valid_format(pref_format)) {
        pref_format = valid_format(info->format) ? info->format
                                                 : video_format::VIDEO_FORMAT_NV12;
    }

    info->format = pref_format;
}

bool lite_h264_video_encoder::i_gpu_encode_available()
{
    return false;
}

void lite_h264_video_encoder::i_update_encode_bitrate(int bitrate)
{
    d_ptr->context->bit_rate = bitrate * 1000;
    d_ptr->context->rc_max_rate = bitrate * 1000;
}

bool lite_h264_video_encoder::update_settings()
{
    int bitrate = lite_obs_encoder_bitrate();
    const char *rc = "CBR";
    int keyint_sec = 2;
#ifdef WIN32
    const char *preset = "hq";
    const char *profile = "high";
#else
    const char *preset = "veryfast";
    const char *profile = "high";
#endif
    int gpu = 0;
    int bf = 0; //todo

    auto video = lite_obs_encoder_video();
    const struct video_output_info *voi = video->video_output_get_info();

    video_scale_info info{};
    info.format = voi->format;
    info.colorspace = voi->colorspace;
    info.range = voi->range;

    i_get_video_info(&info);
    av_opt_set_int(d_ptr->context->priv_data, "cbr", false, 0);
    av_opt_set(d_ptr->context->priv_data, "profile", profile, 0);
    av_opt_set(d_ptr->context->priv_data, "preset", preset, 0);

    av_opt_set_int(d_ptr->context->priv_data, "cbr", true, 0);
    d_ptr->context->rc_max_rate = bitrate * 1000;
    d_ptr->context->rc_min_rate = bitrate * 1000;

#ifdef WIN32
    av_opt_set(d_ptr->context->priv_data, "level", "auto", 0);
    av_opt_set_int(d_ptr->context->priv_data, "2pass", 0, 0);
    av_opt_set_int(d_ptr->context->priv_data, "gpu", gpu, 0);
#else
    d_ptr->context->level = 32;
    av_opt_set_int(d_ptr->context->priv_data, "ndk_codec", true, 0);
#endif

    d_ptr->context->bit_rate = bitrate * 1000;
    d_ptr->context->rc_buffer_size = bitrate * 1000;
    d_ptr->context->width = lite_obs_encoder_get_width();
    d_ptr->context->height = lite_obs_encoder_get_height();
    d_ptr->context->time_base = {(int)voi->fps_den, (int)voi->fps_num};
    d_ptr->context->framerate = {(int)voi->fps_num, (int)voi->fps_den};
    d_ptr->context->pix_fmt = lite_obs_to_ffmpeg_video_format(info.format);
    d_ptr->context->colorspace = info.colorspace == video_colorspace::VIDEO_CS_709
            ? AVCOL_SPC_BT709
            : AVCOL_SPC_BT470BG;
    d_ptr->context->color_range = info.range == video_range_type::VIDEO_RANGE_FULL
            ? AVCOL_RANGE_JPEG
            : AVCOL_RANGE_MPEG;
    d_ptr->context->max_b_frames = bf;

    if (keyint_sec)
        d_ptr->context->gop_size =
                keyint_sec * voi->fps_num / voi->fps_den;
    else
        d_ptr->context->gop_size = 250;

    d_ptr->height = d_ptr->context->height;

    blog(LOG_INFO, "settings:\n"
                   "\trate_control: %s\n"
                   "\tbitrate:      %d\n"
                   "\tkeyint:       %d\n"
                   "\tpreset:       %s\n"
                   "\tprofile:      %s\n"
                   "\twidth:        %d\n"
                   "\theight:       %d\n"
                   "\tb-frames:     %d\n"
                   "\tGPU:          %d\n",
         rc, lite_obs_encoder_bitrate(), d_ptr->context->gop_size, preset, profile,
         d_ptr->context->width, d_ptr->context->height,
         d_ptr->context->max_b_frames, gpu);

    return init_codec();
}

bool lite_h264_video_encoder::init_codec()
{
    auto ret = avcodec_open2(d_ptr->context, d_ptr->codec, NULL);
    if (ret < 0) {
        blog(LOG_WARNING, "Failed to open h264 codec: %d", ret);
        return false;
    }

    d_ptr->vframe = av_frame_alloc();
    if (!d_ptr->vframe) {
        blog(LOG_WARNING, "Failed to allocate video frame");
        return false;
    }

    d_ptr->vframe->format = d_ptr->context->pix_fmt;
    d_ptr->vframe->width = d_ptr->context->width;
    d_ptr->vframe->height = d_ptr->context->height;
    d_ptr->vframe->colorspace = d_ptr->context->colorspace;
    d_ptr->vframe->color_range = d_ptr->context->color_range;

    ret = av_frame_get_buffer(d_ptr->vframe, 32);
    if (ret < 0) {
        blog(LOG_WARNING, "Failed to allocate vframe: %d", ret);
        return false;
    }

    d_ptr->initialized = true;
    return true;
}
