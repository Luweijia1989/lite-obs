#include "lite-obs/encoder/x264_encoder.h"
#include "lite-obs/util/log.h"
#include "lite-obs/media-io/video_output.h"
#include <x264.h>
#include <vector>
#include <cstdint>

static inline const char *get_x264_colorspace_name(video_colorspace cs)
{
    switch (cs) {
    case video_colorspace::VIDEO_CS_DEFAULT:
    case video_colorspace::VIDEO_CS_601:
        return "undef";
    case video_colorspace::VIDEO_CS_709:;
    }

    return "bt709";
}

static inline int get_x264_cs_val(video_colorspace cs, const char *const names[])
{
    const char *name = get_x264_colorspace_name(cs);
    int idx = 0;
    do {
        if (strcmp(names[idx], name) == 0)
            return idx;
    } while (!!names[++idx]);

    return 0;
}

struct x264_encoder_private
{
    x264_param_t params{};
    x264_t *context{};

    std::vector<uint8_t> sei;
    std::vector<uint8_t> extra_data;

    std::shared_ptr<std::vector<uint8_t>> buffer;

    bool initialized{};
    bool first_packet = true;
};

x264_encoder::x264_encoder(lite_obs_encoder *encoder)
    : lite_obs_encoder_interface(encoder)
{
    d_ptr = std::make_unique<x264_encoder_private>();
    d_ptr->buffer = std::make_shared<std::vector<uint8_t>>();
}

x264_encoder::~x264_encoder()
{
    if (x264_encoder::i_encoder_valid())
        x264_encoder::i_destroy();
}

const char *x264_encoder::i_encoder_codec()
{
    return "h264";
}

obs_encoder_type x264_encoder::i_encoder_type()
{
    return obs_encoder_type::OBS_ENCODER_VIDEO;
}

bool x264_encoder::i_encoder_valid()
{
    return d_ptr->initialized;
}

bool x264_encoder::i_create()
{

    if (update_settings(false)) {
        d_ptr->context = x264_encoder_open(&d_ptr->params);

        if (d_ptr->context == NULL)
            blog(LOG_WARNING, "x264 failed to load");
        else
            load_headers();
    } else {
        blog(LOG_WARNING, "bad settings specified");
    }

    if (!d_ptr->context) {
        i_destroy();
        return false;
    }

    d_ptr->initialized = true;
    return true;
}

void x264_encoder::i_destroy()
{
    if (d_ptr->context) {
        x264_encoder_close(d_ptr->context);
        d_ptr->context = NULL;
    }

    d_ptr->extra_data.clear();
    d_ptr->sei.clear();

    d_ptr->initialized = false;
}

void x264_encoder::init_pic_data(x264_picture_t *pic, encoder_frame *frame)
{
    x264_picture_init(pic);

    pic->i_pts = frame->pts;
    pic->img.i_csp = d_ptr->params.i_csp;

    if (d_ptr->params.i_csp == X264_CSP_NV12)
        pic->img.i_plane = 2;
    else if (d_ptr->params.i_csp == X264_CSP_I420)
        pic->img.i_plane = 3;
    else if (d_ptr->params.i_csp == X264_CSP_I444)
        pic->img.i_plane = 3;

    for (int i = 0; i < pic->img.i_plane; i++) {
        pic->img.i_stride[i] = (int)frame->linesize[i];
        pic->img.plane[i] = frame->data[i];
    }
}

void x264_encoder::parse_packet(const std::shared_ptr<encoder_packet> &packet, x264_nal_t *nals, int nal_count, x264_picture_t *pic_out)
{
    if (!nal_count)
        return;

    int bytes = 0;
    for (int i = 0; i < nal_count; i++) {
        x264_nal_t *nal = nals + i;
        bytes += nal->i_payload;
    }

    uint8_t sei_buf[102400] = {0};
    int sei_len = 0;
    bool got_sei = encoder->lite_obs_encoder_get_sei(sei_buf, &sei_len);
    if (got_sei) {
        bytes += sei_len;
    }

    d_ptr->buffer->clear();
    d_ptr->buffer->resize(bytes);

    int copy_index = 0;
    for (int i = 0; i < nal_count; i++) {
        x264_nal_t *nal = nals + i;
        memcpy(d_ptr->buffer->data() + copy_index, nal->p_payload, nal->i_payload);
        copy_index += nal->i_payload;
    }

    if (got_sei)
        memcpy(d_ptr->buffer->data() + copy_index, sei_buf, sei_len);

    packet->data = d_ptr->buffer;
    packet->type = obs_encoder_type::OBS_ENCODER_VIDEO;
    packet->pts = pic_out->i_pts;
    packet->dts = pic_out->i_dts;
    packet->keyframe = pic_out->b_keyframe != 0;
}

bool x264_encoder::i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void(std::shared_ptr<encoder_packet>)> send_off)
{
    if (!frame || !packet)
        return false;

    x264_picture_t pic, pic_out;
    if (frame)
        init_pic_data(&pic, frame);

    x264_nal_t *nals = nullptr;
    int nal_count = 0;
    auto ret = x264_encoder_encode(d_ptr->context, &nals, &nal_count, (frame ? &pic : NULL), &pic_out);
    if (ret < 0) {
        blog(LOG_WARNING, "encode failed");
        return false;
    }

    parse_packet(packet, nals, nal_count, &pic_out);
    if (nal_count != 0) {
        if (d_ptr->first_packet) {
            packet->encoder_first_packet = true;
            d_ptr->first_packet = false;
        }
        send_off(packet);
    }

    return true;
}

bool x264_encoder::i_get_extra_data(uint8_t **extra_data, size_t *size)
{
    *extra_data = d_ptr->extra_data.data();
    *size = d_ptr->extra_data.size();
    return true;
}

bool x264_encoder::i_get_sei_data(uint8_t **sei_data, size_t *size)
{
    *sei_data = d_ptr->sei.data();
    *size = d_ptr->sei.size();
    return true;
}

void x264_encoder::i_get_audio_info(audio_convert_info *info)
{
}

static inline bool valid_format(video_format format)
{
    return format == video_format::VIDEO_FORMAT_I420
           || format == video_format::VIDEO_FORMAT_NV12
           || format == video_format::VIDEO_FORMAT_I444;
}

void x264_encoder::i_get_video_info(video_scale_info *info)
{
    auto pref_format = encoder->lite_obs_encoder_get_preferred_video_format();

    if (!valid_format(pref_format)) {
        pref_format = valid_format(info->format) ? info->format : video_format::VIDEO_FORMAT_NV12;
    }

    info->format = pref_format;
}

bool x264_encoder::i_gpu_encode_available()
{
    return false;
}

void x264_encoder::i_update_encode_bitrate(int bitrate)
{
    if (!d_ptr->context)
        return;

    if(update_settings(true)) {
        auto ret = x264_encoder_reconfig(d_ptr->context, &d_ptr->params);
        if (ret != 0)
            blog(LOG_WARNING, "Failed to reconfigure: %d", ret);
    }
}

bool x264_encoder::update_settings(bool update)
{
    const char *preset = "veryfast";

    if (!update)
        blog(LOG_INFO, "---------------------------------");

    bool success = false;
    if (!d_ptr->context) {
        if (preset && *preset)
            blog(LOG_INFO, "preset: %s", preset);

        int ret = x264_param_default_preset(&d_ptr->params, preset, "");
        success = ret == 0;
    }

    if (success)
        update_params(update);

    d_ptr->params.b_repeat_headers = false;
    return success;
}

static void log_x264(void *param, int level, const char *format, va_list args)
{
    char str[1024] = {};

    vsnprintf(str, 1024, format, args);
    blog(LOG_INFO, "%s", str);

    (void)level;
}

void x264_encoder::update_params(bool update)
{
    auto video = encoder->lite_obs_encoder_video();
    if (!video)
        return;

    auto voi = video->video_output_get_info();
    video_scale_info info;

    info.format = voi->format;
    info.colorspace = voi->colorspace;
    info.range = voi->range;

    i_get_video_info(&info);


    int bitrate = encoder->lite_obs_encoder_bitrate();
    int buffer_size = bitrate;
    int keyint_sec = 2;
    int width = (int)encoder->lite_obs_encoder_get_width();
    int height = (int)encoder->lite_obs_encoder_get_height();

    if (keyint_sec)
        d_ptr->params.i_keyint_max = keyint_sec * voi->fps_num / voi->fps_den;

    d_ptr->params.b_vfr_input = false;
    d_ptr->params.rc.i_vbv_max_bitrate = bitrate;
    d_ptr->params.rc.i_vbv_buffer_size = buffer_size;
    d_ptr->params.rc.i_bitrate = bitrate;
    d_ptr->params.i_width = width;
    d_ptr->params.i_height = height;
    d_ptr->params.i_fps_num = voi->fps_num;
    d_ptr->params.i_fps_den = voi->fps_den;
    d_ptr->params.pf_log = log_x264;
    d_ptr->params.p_log_private = this;
    d_ptr->params.i_log_level = X264_LOG_WARNING;
    d_ptr->params.i_bframe = 0;

    d_ptr->params.vui.i_transfer = get_x264_cs_val(info.colorspace, x264_transfer_names);
    d_ptr->params.vui.i_colmatrix = get_x264_cs_val(info.colorspace, x264_colmatrix_names);
    d_ptr->params.vui.i_colorprim = get_x264_cs_val(info.colorspace, x264_colorprim_names);
    d_ptr->params.vui.b_fullrange = info.range == video_range_type::VIDEO_RANGE_FULL;

    /* use the new filler method for CBR to allow real-time adjusting of
     * the bitrate */
    d_ptr->params.rc.i_rc_method = X264_RC_ABR;

#if X264_BUILD >= 139
    d_ptr->params.rc.b_filler = true;
#else
    d_ptr->params.i_nal_hrd = X264_NAL_HRD_CBR;
#endif

    d_ptr->params.rc.f_rf_constant = (float)0;

    if (info.format == video_format::VIDEO_FORMAT_NV12)
        d_ptr->params.i_csp = X264_CSP_NV12;
    else if (info.format == video_format::VIDEO_FORMAT_I420)
        d_ptr->params.i_csp = X264_CSP_I420;
    else if (info.format == video_format::VIDEO_FORMAT_I444)
        d_ptr->params.i_csp = X264_CSP_I444;
    else
        d_ptr->params.i_csp = X264_CSP_NV12;

    if (!update) {
        blog(LOG_INFO, "settings:\n"
                       "\trate_control: CBR\n"
                       "\tbitrate:      %d\n"
                       "\tbuffer size:  %d\n"
                       "\tcrf:          %d\n"
                       "\tfps_num:      %d\n"
                       "\tfps_den:      %d\n"
                       "\twidth:        %d\n"
                       "\theight:       %d\n"
                       "\tkeyint:       %d\n",
             d_ptr->params.rc.i_vbv_max_bitrate,
             d_ptr->params.rc.i_vbv_buffer_size,
             (int)d_ptr->params.rc.f_rf_constant, voi->fps_num,
             voi->fps_den, width, height, d_ptr->params.i_keyint_max);
    }
}

void x264_encoder::load_headers()
{
    x264_nal_t *nals = nullptr;
    int nal_count = 0;
    x264_encoder_headers(d_ptr->context, &nals, &nal_count);

    for (int i = 0; i < nal_count; i++) {
        x264_nal_t *nal = nals + i;

        std::vector<uint8_t> *container = nullptr;
        if (nal->i_type == NAL_SEI)
            container = &d_ptr->sei;
        else
            container = &d_ptr->extra_data;

        auto l_s = container->size();
        container->resize(nal->i_payload + l_s);
        memcpy(container->data() + l_s, nal->p_payload, nal->i_payload);
    }
}
