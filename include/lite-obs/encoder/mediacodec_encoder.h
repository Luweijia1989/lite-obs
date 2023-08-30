#pragma once

#include "lite-obs/lite_obs_platform_config.h"

#if TARGET_PLATFORM == PLATFORM_ANDROID

#include "lite-obs/lite_encoder.h"

class TextureDrawer
{
public:
    TextureDrawer();
    ~TextureDrawer();
    void draw_texture(int tex_id);
private:
    unsigned int VBO, VAO, EBO;
    unsigned int shader_program{};
};

struct mediacodec_encoder_private;
class mediacodec_encoder : public lite_obs_encoder_interface
{
public:
    mediacodec_encoder(lite_obs_encoder *encoder);
    virtual ~mediacodec_encoder();
    virtual const char *i_encoder_codec();
    virtual obs_encoder_type i_encoder_type();
    virtual void i_set_gs_render_ctx(void *ctx);
    virtual bool i_create();
    virtual void i_destroy();
    virtual bool i_encoder_valid();
    virtual bool i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void(std::shared_ptr<encoder_packet>)> send_off);
    virtual bool i_get_extra_data(uint8_t **extra_data, size_t *size);
    virtual bool i_get_sei_data(uint8_t **sei_data, size_t *size);
    virtual void i_get_audio_info(struct audio_convert_info *info);
    virtual void i_get_video_info(struct video_scale_info *info);
    virtual bool i_gpu_encode_available();
    virtual void i_update_encode_bitrate(int bitrate);

private:
    bool init_mediacodec();
    bool init_egl_related();
    void draw_texture(int tex_id);

private:
    std::unique_ptr<mediacodec_encoder_private> d_ptr{};
};

#endif
