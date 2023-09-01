#pragma once

#include "lite-obs/lite_obs_platform_config.h"

#ifdef PLATFORM_APPLE

#include "lite-obs/lite_encoder.h"
#include <CoreMedia/CMTime.h>

struct videotoolbox_encoder_private;
class videotoolbox_encoder : public lite_obs_encoder_interface
{
public:
    videotoolbox_encoder(lite_obs_encoder *encoder);
    virtual ~videotoolbox_encoder();
    virtual const char *i_encoder_codec();
    virtual obs_encoder_type i_encoder_type();
    virtual void i_set_gs_render_ctx(void *ctx);
    virtual bool i_create();
    virtual void i_destroy();
    virtual bool i_encoder_valid();
    virtual bool i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void(std::shared_ptr<encoder_packet>)> send_off);
    virtual bool i_get_extra_data(uint8_t **extra_data, size_t *size);
    virtual bool i_get_sei_data(uint8_t **sei_data, size_t *size);
    virtual void i_get_video_info(struct video_scale_info *info);
    virtual bool i_gpu_encode_available();
    virtual void i_update_encode_bitrate(int bitrate);

private:
    bool update_params();
    bool create_encoder();
    void dump_encoder_info();
    int session_set_bitrate(void *session);

    bool convert_sample_to_annexb(void *buf, bool keyframe);
    bool parse_sample(void *buf, const std::shared_ptr<encoder_packet> &packet, CMTime off, std::function<void(std::shared_ptr<encoder_packet>)> send_off);

#if TARGET_PLATFORM == PLATFORM_IOS
    bool create_gl_cvpixelbuffer();
    bool create_gl_fbo();
    bool create_texture_encode_resource();
    void draw_video_frame_texture(unsigned int tex_id);
#endif

private:
    std::unique_ptr<videotoolbox_encoder_private> d_ptr{};
};

#endif
