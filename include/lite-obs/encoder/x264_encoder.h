#pragma once

#include "lite-obs/lite_encoder.h"
#include <x264.h>

struct x264_encoder_private;
class x264_encoder : public lite_obs_encoder_interface
{
public:
    x264_encoder(lite_obs_encoder *encoder);
    virtual ~x264_encoder();
    virtual const char *i_encoder_codec();
    virtual obs_encoder_type i_encoder_type();
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
    bool update_settings(bool update);
    void update_params(bool update);
    void load_headers();

    void init_pic_data(x264_picture_t *pic, encoder_frame *frame);
    void parse_packet(const std::shared_ptr<encoder_packet> &packet, x264_nal_t *nals, int nal_count, x264_picture_t *pic_out);

private:
    std::unique_ptr<x264_encoder_private> d_ptr{};
};
