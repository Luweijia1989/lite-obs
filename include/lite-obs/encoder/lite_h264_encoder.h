#pragma once

#include "lite-obs/lite_encoder.h"

struct lite_ffmpeg_video_encoder_private;
class lite_h264_video_encoder : public lite_obs_encoder
{
public:
    lite_h264_video_encoder(int bitrate, size_t mixer_idx);
    virtual ~lite_h264_video_encoder();
    virtual const char *i_encoder_codec();
    virtual obs_encoder_type i_encoder_type();
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
    bool update_settings();
    bool init_codec();

private:
    std::unique_ptr<lite_ffmpeg_video_encoder_private> d_ptr{};
};
